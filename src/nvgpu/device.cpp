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

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#if defined(__linux__)
#include <sys/mman.h>
#elif defined(__SWITCH__)
#include <switch.h>
#endif

#include <nvmap.h>
#include <nvgpu.h>
#include <nvhost_ioctl.h>

#include "../util.hpp"
#include "context.hpp"

namespace envid::nvgpu {

// Random tag to suppress kernel warnings
#define MEM_TAG (0xfeed << 16)

namespace {

[[maybe_unused]]
std::uint32_t get_map_flags(EnvideoMapFlags flags) {
    switch (ENVIDEO_MAP_GET_CPU_FLAGS(flags)) {
        case EnvideoMap_CpuUncacheable:
            return 0; // NVMAP_HANDLE_UNCACHEABLE
        case EnvideoMap_CpuUnmapped:
        case EnvideoMap_CpuWriteCombine:
            return 1; // NVMAP_HANDLE_WRITE_COMBINE
        case EnvideoMap_CpuCacheable:
            return 3; // NVMAP_HANDLE_CACHEABLE
        default:
            return -1;
    }
}

[[maybe_unused]]
std::uint32_t get_heap_mask(EnvideoMapFlags flags) {
    switch (ENVIDEO_MAP_GET_USAGE_FLAGS(flags)) {
        case EnvideoMap_UsageGeneric:
        case EnvideoMap_UsageFramebuffer:
            return 1 << 0;  // NVMAP_HEAP_CARVEOUT_GENERIC
        case EnvideoMap_UsageEngine:
        case EnvideoMap_UsageCmdbuf:
            return 1 << 30; // NVMAP_HEAP_IOVMM
        default:
            return -1;
    }
}

[[maybe_unused]]
int get_cache_op(EnvideoCacheFlags flags) {
    switch (static_cast<int>(flags)) {
        case EnvideoCache_Writeback:
            return NVMAP_CACHE_OP_WB;
        case EnvideoCache_Invalidate:
            return NVMAP_CACHE_OP_INV;
        case EnvideoCache_Writeback | EnvideoCache_Invalidate:
            return NVMAP_CACHE_OP_WB_INV;
        default:
            return -1;
    }
}

NvdecVersion get_nvdec_version(int chip_id) {
    switch (chip_id) {
        case 0x21: // T210
            return NvdecVersion::V20;
        case 0x18: // T186
            return NvdecVersion::V30;
        case 0x19: // T194
            return NvdecVersion::V40;
        case 0x23: // T234
            return NvdecVersion::V50;
        case 0x24: // T241
            // fallthrough, ???
        case 0x26: // T264
            // fallthrough, ???
        default:
            return NvdecVersion::None;
    }
}

NvjpgVersion get_nvjpg_version(int chip_id) {
    switch (chip_id) {
        case 0x21: // T210
            return NvjpgVersion::V10;
        case 0x18: // T186
            return NvjpgVersion::V11;
        case 0x19: // T194
            return NvjpgVersion::V12;
        case 0x23: // T234
            return NvjpgVersion::V13;
        case 0x24: // T241
            // fallthrough, ???
        case 0x26: // T264
            // fallthrough, ???
        default:
            return NvjpgVersion::None;
    }
}

} // namespace

int Device::get_characteristics(nvgpu_gpu_characteristics &characteristics) const {
#if defined(__linux__)
    auto args = nvgpu_gpu_get_characteristics{
        .gpu_characteristics_buf_size = sizeof(characteristics),
        .gpu_characteristics_buf_addr = reinterpret_cast<std::uintptr_t>(&characteristics),
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvhost_gpu_fd, NVGPU_GPU_IOCTL_GET_CHARACTERISTICS, &args));
#elif defined(__SWITCH__)
    // Layout is different between L4T and HOS, just copy a few fields
    auto *c = nvGpuGetCharacteristics();
    characteristics = {
        .big_page_size          = c->big_page_size,
        .flags                  = c->flags,
        .twod_class             = c->twod_class,
        .threed_class           = c->threed_class,
        .compute_class          = c->compute_class,
        .gpfifo_class           = c->gpfifo_class,
        .inline_to_memory_class = c->inline_to_memory_class,
        .dma_copy_class         = c->dma_copy_class,
    };
#endif

    return 0;
}

int Device::alloc_as(std::uint32_t big_page_size) {
#if defined(__linux__)
    auto args = nvgpu_alloc_as_args{
        .big_page_size = big_page_size,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvhost_gpu_fd, NVGPU_GPU_IOCTL_ALLOC_AS, &args));

    this->nvas_fd = args.as_fd;
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvAddressSpaceCreate(&this->gpu_as, big_page_size));
#endif

    return 0;
}

int Device::open_tsg() {
#if defined(__linux__)
    auto args = nvgpu_gpu_open_tsg_args{};
    ENVID_CHECK_ERRNO(::ioctl(this->nvhost_gpu_fd, NVGPU_GPU_IOCTL_OPEN_TSG, &args));

    this->nvtsg_fd = args.tsg_fd;
#endif

    return 0;
}

int Device::free_as() {
#if defined(__linux__)
    if (this->nvas_fd > 0)
        ::close(this->nvas_fd);
#elif defined(__SWITCH__)
    nvAddressSpaceClose(&this->gpu_as);
#endif

    return 0;
}

int Device::close_tsg() {
#if defined(__linux__)
    if (this->nvtsg_fd > 0)
        ::close(this->nvtsg_fd);
#endif

    return 0;
}

int Device::open_gpu_channel(Channel &channel) const {
#if defined(__linux__)
    auto args = nvgpu_gpu_open_channel_args{
        .in = {
            .runlist_id = -1,
        },
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvhost_gpu_fd, NVGPU_GPU_IOCTL_OPEN_CHANNEL, &args));

    channel.fd = args.out.channel_fd;
#endif

    return 0;
}

int Device::bind_channel_as(Channel &channel) const {
#if defined(__linux__)
    auto args = nvgpu_as_bind_channel_args{
        .channel_fd = static_cast<std::uint32_t>(channel.fd),
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvas_fd, NVGPU_AS_IOCTL_BIND_CHANNEL, &args));
#endif

    return 0;
}

int Device::bind_channel_tsg(Channel &channel) const {
#if defined(__linux__)
    ENVID_CHECK_ERRNO(::ioctl(this->nvtsg_fd, NVGPU_TSG_IOCTL_BIND_CHANNEL, &channel.fd));
#endif

    return 0;
}

int Device::map_buffer(Map &map, std::uint64_t &addr, bool cacheable, bool pitch) {
#if defined(__linux__)
    auto args = nvgpu_as_map_buffer_ex_args{
        .flags         = NVGPU_AS_MAP_BUFFER_FLAGS_DIRECT_KIND_CTRL |
                         (cacheable ? NVGPU_AS_MAP_BUFFER_FLAGS_CACHEABLE : UINT32_C(0)),
        .compr_kind    = NV_KIND_INVALID,
        .incompr_kind  = static_cast<std:: int16_t>(pitch ? 0x00 : 0xfe), // NV_MMU_PTE_KIND_PITCH, NV_MMU_PTE_KIND_GENERIC_16BX2
        .dmabuf_fd     = static_cast<std::uint32_t>(map.fd),
        .page_size     = this->page_size,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvas_fd, NVGPU_AS_IOCTL_MAP_BUFFER_EX, &args));

    addr = args.offset;
#elif defined(__SWITCH__)
    iova_t iova;
    ENVID_CHECK_RC(nvAddressSpaceMap(&this->gpu_as, map.handle, cacheable, pitch ? NvKind_Pitch : NvKind_Generic_16BX2, &iova));

    addr = iova;
#endif

    return 0;
}

int Device::unmap_buffer(Map &map, std::uint64_t &addr) {
#if defined(__linux__)
    auto args = nvgpu_as_unmap_buffer_args{
        .offset = addr,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvas_fd, NVGPU_AS_IOCTL_UNMAP_BUFFER, &args));
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvAddressSpaceUnmap(&this->gpu_as, addr));
#endif

    return 0;
}

bool Device::probe() {
#if defined(__linux__)
    struct stat st;

    if (auto rc = ::stat("/dev/nvmap", &st); rc)
        return false;

    if (auto rc = ::stat("/dev/nvhost-ctrl", &st); rc)
        return false;
#elif defined(__SWITCH__)
    bool running = false;
    auto name = smEncodeName("nvdrv");
    Result rc = tipcDispatchInOut(smGetServiceSessionTipc(), 65100, name, running); // AMS extension
    if (R_FAILED(rc) || !running)
        return false;
#endif

    return true;
}

constexpr std::array chip_id_paths = {
    "/sys/module/tegra_fuse/parameters/tegra_chip_id",
    "/sys/module/fuse/parameters/tegra_chip_id",
    "/sys/devices/soc0/soc_id",
};

int Device::initialize() {
#if defined(__linux__)
    std::string buf(0x100, '\0');
    for (auto p: chip_id_paths) {
        auto fd = ::open(p, O_RDONLY);
        if (fd < 0)
            continue;

        ENVID_SCOPEGUARD([fd] { ::close(fd); });

        auto res = ::read(fd, buf.data(), buf.capacity());
        if (res < 0)
            continue;

        this->chip_id = ::strtol(buf.c_str(), nullptr, 0);
        break;
    }
#elif defined(__SWITCH__)
    this->chip_id = 0x21; // T210
#endif

    if (!this->chip_id)
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    this->is_tegra = true;

    this->nvdec_version = get_nvdec_version(this->chip_id);
    this->nvjpg_version = get_nvjpg_version(this->chip_id);

#if defined (__linux__)
    ENVID_CHECK_ERRNO(this->nvhost_fd     = ::open("/dev/nvhost-ctrl",     O_RDWR | O_SYNC | O_CLOEXEC));
    ENVID_CHECK_ERRNO(this->nvmap_fd      = ::open("/dev/nvmap",           O_RDWR | O_SYNC | O_CLOEXEC));
    ENVID_CHECK_ERRNO(this->nvhost_gpu_fd = ::open("/dev/nvhost-ctrl-gpu", O_RDWR | O_SYNC | O_CLOEXEC));
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvInitialize());

    ENVID_CHECK_RC(nvMapInit());
    this->nvmap_fd = nvMapGetFd();

    ENVID_CHECK_RC(nvFenceInit());
    this->nvhost_fd = nvFenceGetFd();

    ENVID_CHECK_RC(nvGpuInit());

    ENVID_CHECK_RC(mmuInitialize());
#endif

    nvgpu_gpu_characteristics characteristics;
    ENVID_CHECK(this->get_characteristics(characteristics));

    if (!(characteristics.flags & NVGPU_GPU_FLAGS_HAS_SYNCPOINTS))
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    this->copy_class = characteristics.dma_copy_class;

    ENVID_CHECK(this->alloc_as(characteristics.big_page_size));
    ENVID_CHECK(this->open_tsg());

    return 0;
}

int Device::finalize() {
    this->close_tsg();
    this->free_as();

#if defined(__linux__)
    if (this->nvhost_gpu_fd > 0) ::close(this->nvhost_gpu_fd);
    if (this->nvhost_fd     > 0) ::close(this->nvhost_fd);
    if (this->nvmap_fd      > 0) ::close(this->nvmap_fd);
#elif defined(__SWITCH__)
    mmuExit();
    nvGpuExit();
    nvFenceExit();
    nvMapExit();
    nvExit();
#endif

    return 0;
}

int Device::wait(envid::Fence fence, std::uint64_t timeout_us) {
    std::uint32_t id = fence_id(fence), value = fence_value(fence);

    // 0 is an invalid syncpt id
    if (!id)
        return ENVIDEO_RC_SYSTEM(EINVAL);

#if defined(__linux__)
    auto args = nvhost_ctrl_syncpt_waitex_args {
        .id      = id,
        .thresh  = value,
        .timeout = static_cast<std::int32_t>(timeout_us),
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvhost_fd, NVHOST_IOCTL_CTRL_SYNCPT_WAITEX, &args));
#elif defined(__SWITCH__)
    auto f = NvFence{
        .id    = id,
        .value = value,
    };
    ENVID_CHECK_RC(nvFenceWait(&f, timeout_us));
#endif

    return 0;
}

int Device::poll(envid::Fence fence, bool &is_done) {
    std::uint32_t id = fence_id(fence), value = fence_value(fence);

    // 0 is an invalid syncpt id
    if (!id)
        return ENVIDEO_RC_SYSTEM(EINVAL);

#if defined(__linux__)
    auto args = nvhost_ctrl_syncpt_read_args{
        .id = id,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->nvhost_fd, NVHOST_IOCTL_CTRL_SYNCPT_READ, &args));

    value = args.value;
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvioctlNvhostCtrl_SyncptRead(this->nvhost_fd, id, &value));
#endif

    // Wrapping comparison
    is_done = static_cast<std::int32_t>(value - fence_value(fence)) >= 0;
    return 0;
}

int Map::get_fd() {
#if defined(__linux__)
    auto &d = *reinterpret_cast<Device *>(this->device);

    auto args = nvmap_create_handle{
        .handle = this->handle,
    };
    ENVID_CHECK_ERRNO(::ioctl(d.nvmap_fd, NVMAP_IOC_GET_FD, &args));

    this->fd = args.fd;
#endif

    return 0;
}

int Map::map_cpu() {
#if defined(__linux__)
    auto addr = ::mmap(nullptr, this->size, PROT_READ | PROT_WRITE, MAP_SHARED, this->handle, 0);
    if (addr == MAP_FAILED)
        return ENVIDEO_RC_SYSTEM(errno);

    this->cpu_addr = addr;
#endif

    return 0;
}

int Map::map_gpu() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    bool is_gpu_cached = ENVIDEO_MAP_GET_GPU_FLAGS(this->flags) == EnvideoMap_GpuCacheable;
    ENVID_CHECK(d.map_buffer(*this, this->gpu_addr_pitch, is_gpu_cached, true));

    if (ENVIDEO_MAP_GET_USAGE_FLAGS(this->flags) == EnvideoMap_UsageFramebuffer)
        ENVID_CHECK(d.map_buffer(*this, this->gpu_addr_block, is_gpu_cached, false));

    return 0;
}

int Map::unmap_cpu() {
#if defined(__linux__)
    if (this->cpu_addr) {
        ::munmap(this->cpu_addr, this->size);
        this->cpu_addr = nullptr;
    }
#endif

    return 0;
}

int Map::unmap_gpu() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    if (this->gpu_addr_pitch) {
        d.unmap_buffer(*this, this->gpu_addr_pitch);
        this->gpu_addr_pitch = 0;
    }

    if (this->gpu_addr_block) {
        d.unmap_buffer(*this, this->gpu_addr_block);
        this->gpu_addr_block = 0;
    }

    return 0;
}

int Map::initialize(size_t size, size_t align) {
    auto &d = *reinterpret_cast<Device *>(this->device);

#if defined(__linux__)
    auto create_args = nvmap_create_handle{
        .size = static_cast<std::uint32_t>(size),
    };
    ENVID_CHECK_ERRNO(::ioctl(d.nvmap_fd, NVMAP_IOC_CREATE, &create_args));

    this->size   = size;
    this->handle = create_args.handle;

    auto guard = util::ScopeGuard([this] { this->finalize(); });

    auto map_flags = get_map_flags(this->flags);
    auto heap_mask = get_heap_mask(this->flags);
    if ((map_flags == UINT32_MAX) || (heap_mask == UINT32_MAX))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    auto alloc_args = nvmap_alloc_handle{
        .handle    = this->handle,
        .heap_mask = heap_mask,
        .flags     = map_flags | MEM_TAG,
        .align     = static_cast<std::uint32_t>(align),
    };
    ENVID_CHECK_ERRNO(::ioctl(d.nvmap_fd, NVMAP_IOC_ALLOC, &alloc_args));
#elif defined(__SWITCH__)
    size  = util::align_up(size,  d.page_size);
    align = util::align_up(align, d.page_size);

    this->alloc = ::aligned_alloc(align, size);
    if (!this->alloc)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    auto guard = util::ScopeGuard([this] { this->finalize(); });

    bool is_cpu_cached = ENVIDEO_MAP_GET_CPU_FLAGS(this->flags) == EnvideoMap_CpuCacheable;
    ENVID_CHECK_RC(nvMapCreate(&this->map, this->alloc, size, align, NvKind_Pitch, is_cpu_cached));

    if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) != EnvideoMap_CpuUnmapped)
        this->cpu_addr = this->alloc;

    this->size   = size;
    this->handle = this->map.handle;
#endif

    ENVID_CHECK(this->get_fd());

    if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) != EnvideoMap_CpuUnmapped)
        ENVID_CHECK(this->map_cpu());

    if (ENVIDEO_MAP_GET_GPU_FLAGS(flags) != EnvideoMap_GpuUnmapped)
        ENVID_CHECK(this->map_gpu());

    guard.cancel();

    return 0;
}

int Map::initialize(void *address, std::size_t size, std::size_t align) {
    auto &d = *reinterpret_cast<Device *>(this->device);

#if defined(__linux__)
    auto map_flags = get_map_flags(this->flags);
    if (map_flags == UINT32_MAX)
        return ENVIDEO_RC_SYSTEM(EINVAL);

    auto args = nvmap_create_handle_from_va{
        .va    = reinterpret_cast<std::uintptr_t>(address),
        .size  = static_cast<std::uint32_t>(size),
        .flags = map_flags | MEM_TAG,
    };
    ENVID_CHECK_ERRNO(::ioctl(d.nvmap_fd, NVMAP_IOC_FROM_VA, &args));

    this->size   = size;
    this->handle = args.handle;

    auto guard = util::ScopeGuard([this] { this->finalize(); });
#elif defined(__SWITCH__)
    size  = util::align_up(size,  d.page_size);
    align = util::align_up(align, d.page_size);

    bool is_cpu_cached = ENVIDEO_MAP_GET_CPU_FLAGS(this->flags) == EnvideoMap_CpuCacheable;
    ENVID_CHECK_RC(nvMapCreate(&this->map, address, size, align, NvKind_Pitch, is_cpu_cached));

    if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) != EnvideoMap_CpuUnmapped) {
        this->cpu_addr = address;
        this->own_mem  = false;
    }

    this->size     = size;
    this->handle   = this->map.handle;

    auto guard = util::ScopeGuard([this] { this->finalize(); });
#endif

    ENVID_CHECK(this->get_fd());

    if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) != EnvideoMap_CpuUnmapped)
        ENVID_CHECK(this->map_cpu());

    if (ENVIDEO_MAP_GET_GPU_FLAGS(flags) != EnvideoMap_GpuUnmapped)
        ENVID_CHECK(this->map_gpu());

    guard.cancel();

    return 0;
}

int Map::finalize() {
    this->unmap_gpu();
    this->unmap_cpu();

#if defined(__linux__)
    auto &d = *reinterpret_cast<Device *>(this->device);

    if (this->fd) {
        ::close(this->fd);
        this->fd = 0;
    }

    if (this->handle) {
        ::ioctl(d.nvmap_fd, NVMAP_IOC_FREE, this->handle);
        this->handle = 0;
    }
#elif defined(__SWITCH__)
    for (auto &&[c, i]: this->pins) {
        auto *chan = reinterpret_cast<Channel *>(c);
        auto args = nvioctl_command_buffer_map{
            .handle = this->handle,
            .iova   = static_cast<std::uint32_t>(i),
        };
        nvioctlChannel_UnmapCommandBuffer(chan->fd, &args, 1, false);
    }

    if (this->map.handle)
        nvMapClose(&this->map);

    if (this->own_mem && this->alloc)
        ::free(this->alloc);
#endif

    return 0;
}

int Map::pin(envid::Channel *channel) {
    // On old L4T versions, pinning a map to a channel address space is not possible,
    // instead clients must use the relocation mechanism when building command buffers
    // On later versions, NVHOST_IOCTL_CHANNEL_MAP_BUFFER could possibly be used?
#if defined(__SWITCH__)
    if (!engine_is_multimedia(channel->engine))
        return 0;

    auto *c = reinterpret_cast<Channel *>(channel);

    auto args = nvioctl_command_buffer_map{
        .handle = this->handle,
    };
    ENVID_CHECK_RC(nvioctlChannel_MapCommandBuffer(c->fd, &args, 1, false));

    this->pins.emplace_back(channel, args.iova);
#endif

    return 0;
}

int Map::cache_op(std::size_t offset, std::size_t len, EnvideoCacheFlags flags) {
#if defined(__linux__)
    auto &d = *reinterpret_cast<Device *>(this->device);

    auto op = get_cache_op(flags);
    if (flags < 0)
        return ENVIDEO_RC_SYSTEM(EINVAL);

    auto args = nvmap_cache_op{
        .addr   = reinterpret_cast<std::uintptr_t>(this->cpu_addr) + offset,
        .handle = this->handle,
        .len    = static_cast<std::uint32_t>(len),
        .op     = op,
    };
    ENVID_CHECK_ERRNO(::ioctl(d.nvmap_fd, NVMAP_IOC_CACHE, &args));
#elif defined(__SWITCH__)
    auto addr = static_cast<std::uint8_t *>(this->cpu_addr) + offset;
    switch (static_cast<int>(flags)) {
        case EnvideoCache_Writeback:
            armDCacheClean(addr, len);
            break;
        case EnvideoCache_Invalidate:
        case EnvideoCache_Writeback | EnvideoCache_Invalidate:
            armDCacheFlush(addr, len);
            break;
        default:
            return -1;
    }
#endif

    return 0;
}

} // namespace envid::nvgpu
