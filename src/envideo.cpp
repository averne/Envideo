/*
 * Copyright (c) 2025 averne <averne381@gmail.com>
 *
 * This file is part of Envideo.

 * Envideo is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License,
 * or (at your option) any later version.

 * Envideo is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with Envideo. If not, see <https://www.gnu.org/licenses/>.
 */

#include <cstring>
#include <cmath>
#include <algorithm>
#include <bit>

#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>

#include <envideo.h>

#include <config.h>

#include "common.hpp"
#include "util.hpp"

#ifdef CONFIG_NVIDIA
#include "nvidia/context.hpp"
#endif
#ifdef CONFIG_NVGPU
#include "nvgpu/context.hpp"
#endif

#include <nvmisc.h>
#include <clc7b5.h>

int envideo_device_create(EnvideoDevice **device) {
    if (!device) return ENVIDEO_RC_SYSTEM(EINVAL);
    *device = nullptr;

    EnvideoPlatform p;
    envid::Device *dev = nullptr;

    if (false);
#ifdef CONFIG_NVIDIA
    else if (auto res = envid::nvidia::Device::probe(); res) {
        dev = new envid::nvidia::Device();
        p   = static_cast<EnvideoPlatform>(EnvideoPlatform_Linux | EnvideoPlatform_Nvidia);
    }
#endif
#ifdef CONFIG_NVGPU
    else if (auto res = envid::nvgpu::Device::probe(); res) {
        dev = new envid::nvgpu::Device();
#if defined(__linux__)
        p   = static_cast<EnvideoPlatform>(EnvideoPlatform_Linux | EnvideoPlatform_Nvgpu);
#elif defined(__SWITCH__)
        p   = static_cast<EnvideoPlatform>(EnvideoPlatform_Hos | EnvideoPlatform_Nvgpu);
#endif
    }
#endif
    else
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    if (!dev)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    auto guard = envid::util::ScopeGuard([dev] { dev->finalize(); delete dev; });

    dev->platform = p;

#if defined(__linux__)
    ENVID_CHECK_ERRNO(dev->page_size = ::sysconf(_SC_PAGESIZE));
#elif defined(__SWITCH__)
    dev->page_size = 0x1000;
#endif

    ENVID_CHECK(dev->initialize());

    *device = reinterpret_cast<EnvideoDevice *>(dev);
    guard.cancel();

    return 0;
}

int envideo_device_destroy(EnvideoDevice *device) {
    if (!device) return ENVIDEO_RC_SYSTEM(EINVAL);
    ENVID_SCOPEGUARD([device] { delete device; });
    return device->finalize();
}

EnvideoDeviceInfo envideo_device_get_info(EnvideoDevice *device) {
    if (!device) return {};

    return {
        .is_tegra = device->is_tegra,
    };
}

int envideo_fence_wait(EnvideoDevice *device, EnvideoFence fence, std::uint64_t timeout_us) {
    return device ? device->wait(fence, timeout_us) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_fence_poll(EnvideoDevice *device, EnvideoFence fence, bool *is_done) {
    if (!device || !is_done) return ENVIDEO_RC_SYSTEM(EINVAL);

    *is_done = false;
    return device->poll(fence, *is_done);
}

int envideo_map_create(EnvideoDevice *device, EnvideoMap **map,
                       std::size_t size, std::size_t align, EnvideoMapFlags flags)
{
    if (!device || !map) return ENVIDEO_RC_SYSTEM(EINVAL);

    if (!size || !align || (align & (align - 1)))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    *map = nullptr;

    envid::Map *m = nullptr;
    switch (ENVIDEO_PLATFORM_GET_DRIVER(device->platform)) {
#ifdef CONFIG_NVIDIA
        case EnvideoPlatform_Nvidia:
            m = new envid::nvidia::Map(device, flags);
            break;
#endif
#ifdef CONFIG_NVGPU
        case EnvideoPlatform_Nvgpu:
            m = new envid::nvgpu::Map(device, flags);
            break;
#endif
        default:
            break;
    }

    if (!m)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    auto guard = envid::util::ScopeGuard([m] { m->finalize(); delete m; });

    ENVID_CHECK(m->initialize(size, align));

    *map = reinterpret_cast<EnvideoMap *>(m);
    guard.cancel();

    return 0;
}

int envideo_map_from_va(EnvideoDevice *device, EnvideoMap **map, void *mem,
                        std::size_t size, std::size_t align, EnvideoMapFlags flags)
{
    if (!device || !map || !mem) return ENVIDEO_RC_SYSTEM(EINVAL);

    *map = nullptr;

    envid::Map *m = nullptr;
    switch (ENVIDEO_PLATFORM_GET_DRIVER(device->platform)) {
#ifdef CONFIG_NVIDIA
        case EnvideoPlatform_Nvidia:
            m = new envid::nvidia::Map(device, flags);
            break;
#endif
#ifdef CONFIG_NVGPU
        case EnvideoPlatform_Nvgpu:
            m = new envid::nvgpu::Map(device, flags);
            break;
#endif
        default:
            break;
    }

    if (!m)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    auto guard = envid::util::ScopeGuard([m] { m->finalize(); delete m; });

    ENVID_CHECK(m->initialize(mem, size, align));

    *map = reinterpret_cast<EnvideoMap *>(m);
    guard.cancel();

    return 0;
}

int envideo_map_destroy(EnvideoMap *map) {
    if (!map) return ENVIDEO_RC_SYSTEM(EINVAL);
    ENVID_SCOPEGUARD([map] { delete map; });
    return map->finalize();
}

int envideo_map_realloc(EnvideoMap *map, std::size_t size, std::size_t align) {
    if (!map || map->size >= size) return ENVIDEO_RC_SYSTEM(EINVAL);

    EnvideoMap *m;
    ENVID_CHECK(envideo_map_create(reinterpret_cast<EnvideoDevice *>(map->device), &m, size, align, map->flags));

    auto guard = envid::util::ScopeGuard([m] { envideo_map_destroy(m); });

    for (auto &&[c, _]: map->pins)
        ENVID_CHECK(m->pin(c));

    std::memcpy(m->cpu_addr, map->cpu_addr, std::min(m->size, map->size));
    ENVID_CHECK(map->finalize());

    switch (ENVIDEO_PLATFORM_GET_DRIVER(map->device->platform)) {
#ifdef CONFIG_NVIDIA
        case EnvideoPlatform_Nvidia:
            *reinterpret_cast<envid::nvidia::Map *>(map) = *reinterpret_cast<envid::nvidia::Map *>(m);
            break;
#endif
#ifdef CONFIG_NVGPU
        case EnvideoPlatform_Nvgpu:
            *reinterpret_cast<envid::nvgpu::Map *>(map) = *reinterpret_cast<envid::nvgpu::Map *>(m);
            break;
#endif
        default:
            break;
    }

    guard.cancel();

    delete m;
    return 0;
}

int envideo_map_pin(EnvideoMap *map, EnvideoChannel *channel) {
    if (!map || !channel) return ENVIDEO_RC_SYSTEM(EINVAL);

    // Avoid multiple mappings to the same channel
    if (map->find_pin(channel) != 0)
        return 0;

    return map->pin(channel);
}

int envideo_map_cache_op(EnvideoMap *map, std::size_t offset, std::size_t len,
                         EnvideoCacheFlags flags)
{
    if (!map || !flags)
        return ENVIDEO_RC_SYSTEM(EINVAL);

    switch (ENVIDEO_MAP_GET_CPU_FLAGS(map->flags)) {
        case EnvideoMap_CpuCacheable:
            return map->cache_op(offset, len, flags);
        case EnvideoMap_CpuWriteCombine:
            envid::util::write_fence(); // fallthrough
        case EnvideoMap_CpuUncacheable:
        case EnvideoMap_CpuUnmapped:
            return 0;
        default:
            return ENVIDEO_RC_SYSTEM(EINVAL);
    }
}

std::size_t envideo_map_get_size(EnvideoMap *map) {
    return map ? map->size : 0;
}

std::uint32_t envideo_map_get_handle(EnvideoMap *map) {
    return map ? map->handle : 0;
}

void *envideo_map_get_cpu_addr(EnvideoMap *map) {
    return map ? map->cpu_addr : 0;
}

std::uint64_t envideo_map_get_gpu_addr(EnvideoMap *map) {
    return map ? map->gpu_addr_pitch : 0;
}

int envideo_channel_create(EnvideoDevice *device, EnvideoChannel **channel, EnvideoEngine engine) {
    if (!device || !channel) return ENVIDEO_RC_SYSTEM(EINVAL);

    *channel = nullptr;

    if (engine == EnvideoEngine_Host)
        return ENVIDEO_RC_SYSTEM(EINVAL);

    envid::Channel *chan = nullptr;
    switch (ENVIDEO_PLATFORM_GET_DRIVER(device->platform)) {
#ifdef CONFIG_NVIDIA
        case EnvideoPlatform_Nvidia:
            chan = new envid::nvidia::Channel(device, engine);
            break;
#endif
#ifdef CONFIG_NVGPU
        case EnvideoPlatform_Nvgpu:
            chan = new envid::nvgpu::Channel(device, engine);
            break;
#endif
        default:
            break;
    }

    if (!chan)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    auto guard = envid::util::ScopeGuard([chan] { chan->finalize(); delete chan; });

    chan->engine = engine;
    ENVID_CHECK(chan->initialize());

    *channel = reinterpret_cast<EnvideoChannel *>(chan);
    guard.cancel();

    return 0;
}

int envideo_channel_destroy(EnvideoChannel *channel) {
    if (!channel) return ENVIDEO_RC_SYSTEM(EINVAL);
    ENVID_SCOPEGUARD([channel] { delete channel; });
    return channel->finalize();
}

int envideo_channel_submit(EnvideoChannel *channel, EnvideoCmdbuf *cmdbuf, EnvideoFence *fence) {
    if (!channel || !cmdbuf || !fence) return ENVIDEO_RC_SYSTEM(EINVAL);

    // Flush CPU writes to the command buffer
    if (ENVIDEO_MAP_GET_CPU_FLAGS(cmdbuf->map->flags) != EnvideoMap_CpuUncacheable)
        envid::util::write_fence();

    *fence = 0;
    return channel->submit(cmdbuf, fence);
}

int envideo_cmdbuf_create(EnvideoChannel *channel, EnvideoCmdbuf **cmdbuf) {
    if (!channel || !cmdbuf) return ENVIDEO_RC_SYSTEM(EINVAL);

    auto *c = channel->create_cmdbuf();
    if (!c)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    auto guard = envid::util::ScopeGuard([c] { c->finalize(); delete c; });

    ENVID_CHECK(c->initialize());

    *cmdbuf = reinterpret_cast<EnvideoCmdbuf *>(c);
    guard.cancel();

    return 0;
}

int envideo_cmdbuf_destroy(EnvideoCmdbuf *cmdbuf) {
    if (!cmdbuf) return ENVIDEO_RC_SYSTEM(EINVAL);
    ENVID_SCOPEGUARD([cmdbuf] { delete cmdbuf; });
    return cmdbuf->finalize();
}

int envideo_cmdbuf_add_memory(EnvideoCmdbuf *cmdbuf, const EnvideoMap *map, std::uint32_t offset, std::uint32_t size) {
    return (cmdbuf && map) ? cmdbuf->add_memory(map, offset, size) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_clear(EnvideoCmdbuf *cmdbuf) {
    return cmdbuf ? cmdbuf->clear() : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_begin(EnvideoCmdbuf *cmdbuf, EnvideoEngine engine) {
    return cmdbuf ? cmdbuf->begin(engine) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_end(EnvideoCmdbuf *cmdbuf) {
    return cmdbuf ? cmdbuf->end() : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_push_word(EnvideoCmdbuf *cmdbuf, std::uint32_t word) {
    return cmdbuf ? cmdbuf->push_word(word) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_push_value(EnvideoCmdbuf *cmdbuf, std::uint32_t offset, std::uint32_t value) {
    return cmdbuf ? cmdbuf->push_value(offset, value) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_push_reloc(EnvideoCmdbuf *cmdbuf, std::uint32_t offset, const EnvideoMap *target,
                              std::uint32_t target_offset, EnvideoRelocType reloc_type, int shift)
{
    return (cmdbuf && target) ? cmdbuf->push_reloc(offset, target, target_offset, reloc_type, shift) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_wait_fence(EnvideoCmdbuf *cmdbuf, EnvideoFence fence) {
    return cmdbuf ? cmdbuf->wait_fence(fence) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_cmdbuf_cache_op(EnvideoCmdbuf *cmdbuf, EnvideoCacheFlags flags) {
    return cmdbuf ? cmdbuf->cache_op(flags) : ENVIDEO_RC_SYSTEM(EINVAL);
}

int envideo_dfs_initialize(EnvideoChannel *channel, float framerate) {
    // Use 10Hz as fallback if no framerate information is available
    channel->dfs_framerate         = (framerate >= 0.1 && std::isfinite(framerate)) ? framerate : 10.0;
    channel->dfs_num_samples       = 0;
    channel->dfs_bitrate_sum       = 0;
    channel->dfs_sampling_start_ts = std::chrono::system_clock::now();
    return 0;
}

int envideo_dfs_finalize(EnvideoChannel *channel) {
    return channel->set_clock_rate(0);
}

int envideo_dfs_set_damping(EnvideoChannel *channel, double damping) {
    channel->dfs_ema_damping = damping;
    return 0;
}

int envideo_dfs_update(EnvideoChannel *channel, int len, int cycles) {
    // Official software implements DFS using a flat average of the decoder pool occupancy.
    // We instead use the decode cycles as reported by NVDEC microcode, and the "bitrate"
    // (bitstream bits fed to the hardware in a given wall time interval, NOT video time),
    // to calculate a suitable frequency, and multiply it by 1.2 for good measure:
    //   freq = decode_cycles_per_bit * bits_per_second * 1.2

    // Convert to bits
    len *= 8;

    // Exponential moving average of decode cycles per frame
    // If this is the first sample ever, use it to initialize the ema value
    auto cyc_per_bit = static_cast<double>(cycles) / len;
    if (channel->dfs_decode_cycles_ema == 0.0)
        channel->dfs_decode_cycles_ema = cyc_per_bit;
    else
        channel->dfs_decode_cycles_ema = channel->dfs_ema_damping * cyc_per_bit +
            (1.0 - channel->dfs_ema_damping) * channel->dfs_decode_cycles_ema;

    channel->dfs_bitrate_sum += len;
    channel->dfs_num_samples++;

    return 0;
}

int envideo_dfs_commit(EnvideoChannel *channel) {
    // If we didn't collect enough samples yet, do nothing
    if (channel->dfs_num_samples < envid::Channel::dfs_samples_threshold)
        return 0;

    auto now   = std::chrono::system_clock::now();
    auto dt    = now - channel->dfs_sampling_start_ts;
    auto wl_dt = std::chrono::duration_cast<std::chrono::microseconds>(dt).count();

    // Try to filter bad sample sets caused by eg. pausing the video playback.
    // We reject the set if one of these conditions is met:
    // - the wall time is over 1.5x the framerate
    // - the wall time is over 1.5x the ema-damped previous values

    double frame_time = 1.0e6 / channel->dfs_framerate;

    int rc = 0;
    if ((wl_dt / channel->dfs_num_samples < 1.5 * frame_time) ||
            (channel->dfs_last_ts_delta && (wl_dt < 1.5 * channel->dfs_last_ts_delta))) {
        auto avg   = channel->dfs_bitrate_sum * 1e6 / wl_dt;
        auto clock = channel->dfs_decode_cycles_ema * avg * 1.2;

        rc = channel->set_clock_rate(clock);

        channel->dfs_last_ts_delta = wl_dt;
    }

    channel->dfs_num_samples       = 0;
    channel->dfs_bitrate_sum       = 0;
    channel->dfs_sampling_start_ts = now;

    return rc;
}

int envideo_surface_transfer(EnvideoCmdbuf *cmdbuf, EnvideoSurfaceInfo *src, EnvideoSurfaceInfo *dst) {
    auto flags = DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NON_PIPELINED) |
                 DRF_DEF(C7B5, _LAUNCH_DMA, _FLUSH_ENABLE,       _TRUE)          |
                 DRF_DEF(C7B5, _LAUNCH_DMA, _MULTI_LINE_ENABLE,  _TRUE);

    ENVID_CHECK(cmdbuf->begin(EnvideoEngine_Copy));

    ENVID_CHECK(cmdbuf->push_reloc(NVC7B5_OFFSET_IN_UPPER,  src->map, src->map_offset,
        !src->tiled ? EnvideoRelocType_Pitch : EnvideoRelocType_Tiled, 0));
    ENVID_CHECK(cmdbuf->push_reloc(NVC7B5_OFFSET_OUT_UPPER, dst->map, dst->map_offset,
        !dst->tiled ? EnvideoRelocType_Pitch : EnvideoRelocType_Tiled, 0));

    if (src->tiled) {
        flags |= DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT, _BLOCKLINEAR);
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_SRC_BLOCK_SIZE,
            DRF_DEF(C7B5, _SET_SRC_BLOCK_SIZE, _WIDTH,      _ONE_GOB)                          |
            DRF_NUM(C7B5, _SET_SRC_BLOCK_SIZE, _HEIGHT,     std::countr_zero(src->gob_height)) |
            DRF_DEF(C7B5, _SET_SRC_BLOCK_SIZE, _DEPTH,      _ONE_GOB)                          |
            DRF_DEF(C7B5, _SET_SRC_BLOCK_SIZE, _GOB_HEIGHT, _GOB_HEIGHT_FERMI_8)));
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_SRC_WIDTH,  src->stride));
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_SRC_HEIGHT, src->height));
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_SRC_DEPTH,  1));
    } else {
        flags |= DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT, _PITCH);
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_PITCH_IN, src->stride));
    }

    if (dst->tiled) {
        flags |= DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT, _BLOCKLINEAR);
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_DST_BLOCK_SIZE,
            DRF_DEF(C7B5, _SET_DST_BLOCK_SIZE, _WIDTH,      _ONE_GOB)                          |
            DRF_NUM(C7B5, _SET_DST_BLOCK_SIZE, _HEIGHT,     std::countr_zero(dst->gob_height)) |
            DRF_DEF(C7B5, _SET_DST_BLOCK_SIZE, _DEPTH,      _ONE_GOB)                          |
            DRF_DEF(C7B5, _SET_DST_BLOCK_SIZE, _GOB_HEIGHT, _GOB_HEIGHT_FERMI_8)));
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_DST_WIDTH,  dst->stride));
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_DST_HEIGHT, dst->height));
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_SET_DST_DEPTH,  1));
    } else {
        flags |= DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT, _PITCH);
        ENVID_CHECK(cmdbuf->push_value(NVC7B5_PITCH_OUT, dst->stride));
    }

    ENVID_CHECK(cmdbuf->push_value(NVC7B5_LINE_LENGTH_IN, src->width));
    ENVID_CHECK(cmdbuf->push_value(NVC7B5_LINE_COUNT,     std::min(src->height, dst->height)));

    ENVID_CHECK(cmdbuf->push_value(NVC7B5_LAUNCH_DMA, flags));

    ENVID_CHECK(cmdbuf->end());

    return 0;
}

int envideo_get_decode_constraints(EnvideoDevice *device, EnvideoDecodeConstraints *constraints) {
    return (device && constraints) ? envid::get_decode_constraints(device, constraints) : ENVIDEO_RC_SYSTEM(EINVAL);
}
