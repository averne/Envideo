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

#pragma once

#include <cstdint>
#include <chrono>
#include <vector>
#include <utility>

#include <envideo.h>

#include <host1x.h>

namespace envid {

enum class NvdecVersion {
    None,
    V10, V11,
    V20,
    V30, V31, V32,
    V40, V41, V42,
    V50, V51,
    V60, V61, V62,
};

enum class NvencVersion {
    None,
};

enum class NvjpgVersion {
    None,
    V10, V11, V12, V13,
};

class Device;
class Channel;
class Map;
class Cmdbuf;

using Fence = EnvideoFence;

constexpr Fence make_fence(std::uint32_t id, std::uint32_t value) {
    return (static_cast<Fence>(id) << 32) | static_cast<Fence>(value);
}

constexpr std::uint32_t fence_value(Fence fence) {
    return fence;
}

constexpr std::uint32_t fence_id(Fence fence) {
    return fence >> 32;
}

class Device {
    public:
        virtual    ~Device()                                           = default;
        virtual int initialize()                                       = 0;
        virtual int finalize()                                         = 0;
        virtual int wait(envid::Fence fence, std::uint64_t timeout_us) = 0;
        virtual int poll(envid::Fence fence, bool &is_done)            = 0;

        virtual const Map *get_semaphore_map() const = 0;

    public:
        std::uint32_t page_size = 0;

        EnvideoPlatform platform;
        NvdecVersion nvdec_version = NvdecVersion::None;
        NvencVersion nvenc_version = NvencVersion::None;
        NvjpgVersion nvjpg_version = NvjpgVersion::None;

        bool tegra_layout = false;
        bool vp8_unsupported = false, vp9_unsupported  = false, vp9_high_depth_unsupported = false,
            h264_unsupported = false, hevc_unsupported = false, av1_unsupported            = false;
};

class Channel {
    public:
        constexpr static auto dfs_samples_threshold = 10;

        enum class Type {
            Gpfifo,
            Host1x,
        };

    public:
        Channel(envid::Device *device, EnvideoEngine engine): device(device), engine(engine) { }
        virtual        ~Channel()                                          = default;
        virtual int     initialize()                                       = 0;
        virtual int     finalize()                                         = 0;
        virtual Cmdbuf *create_cmdbuf()                                    = 0;
        virtual int     submit(envid::Cmdbuf *cmdbuf, envid::Fence *fence) = 0;
        virtual int     get_clock_rate(std::uint32_t &clock)               = 0;
        virtual int     set_clock_rate(std::uint32_t clock)                = 0;

    public:
        Device       *device = nullptr;
        EnvideoEngine engine;
        Type          type;

        float         dfs_framerate         = 0.0;
        double        dfs_decode_cycles_ema = 0.0;
        double        dfs_ema_damping       = 0.1;
        std::uint32_t dfs_num_samples       = 0,
                      dfs_bitrate_sum       = 0;

        std::chrono::system_clock::time_point dfs_sampling_start_ts;
        std::int64_t dfs_last_ts_delta = 0;
};

class Map {
    public:
        Map(envid::Device *device, EnvideoMapFlags flags): device(device), flags(flags) { }
        virtual    ~Map()                                                          = default;
        virtual int initialize(std::size_t size, std::size_t align)                = 0;
        virtual int initialize(void *address, std::size_t size, std::size_t align) = 0;
        virtual int finalize()                                                     = 0;
        virtual int pin(envid::Channel *channel)                                   = 0;
        virtual int cache_op(std::size_t offset, std::size_t len,
                             EnvideoCacheFlags flags)                              = 0;

    public:
        std::uint64_t find_pin(Channel *channel) const {
            auto res = std::ranges::find_if(this->pins,
                [channel](auto &p) { return p.first == channel; });
            return (res != this->pins.end()) ? res->second : 0;
        }

        std::uint64_t find_pin(EnvideoEngine engine) const {
            auto res = std::ranges::find_if(this->pins,
                [engine](auto &p) { return p.first->engine == engine; });
            return (res != this->pins.end()) ? res->second : 0;
        }

    public:
        Device         *device = nullptr;
        EnvideoMapFlags flags;

        bool          own_mem  = true;
        std::uint32_t handle   = 0;
        std::size_t   size     = 0;
        void         *cpu_addr = nullptr;
        std::uint64_t gpu_addr_pitch = 0,
            gpu_addr_block = 0;

        std::vector<std::pair<envid::Channel *, std::uint64_t>> pins;
};

class Cmdbuf {
    public:
        virtual    ~Cmdbuf()                                              = default;
        virtual int initialize()                                          = 0;
        virtual int finalize()                                            = 0;
        virtual int add_memory(const envid::Map *map, std::uint32_t offset,
                               std::uint32_t size);
        virtual int clear()                                               = 0;
        virtual int begin(EnvideoEngine engine)                           = 0;
        virtual int end()                                                 = 0;
        virtual int push_word(std::uint32_t word)                         = 0;
        virtual int push_value(std::uint32_t offset, std::uint32_t value) = 0;
        virtual int push_reloc(std::uint32_t offset, const envid::Map *target,
                               std::uint32_t target_offset,
                               EnvideoRelocType reloc_type, int shift)    = 0;
        virtual int wait_fence(envid::Fence fence)                        = 0;
        virtual int cache_op(EnvideoCacheFlags flags)                     = 0;

        std::uint32_t *words() const {
            auto mem = reinterpret_cast<std::uintptr_t>(this->map->cpu_addr);
            return reinterpret_cast<std::uint32_t *>(mem + this->mem_offset);
        }

        std::size_t num_words() const {
            return this->cur_word - this->words();
        }

    public:
        const Map     *map        = nullptr;
        std::uint32_t  mem_offset = 0,
                       mem_size   = 0;

    protected:
        EnvideoEngine  cur_engine;
        std::uint32_t *cur_word   = 0;
};

constexpr bool engine_is_multimedia(EnvideoEngine engine) {
    switch (engine) {
        case EnvideoEngine_Nvdec:
        case EnvideoEngine_Nvenc:
        case EnvideoEngine_Nvjpg:
        case EnvideoEngine_Vic:
        case EnvideoEngine_Ofa:
            return true;
        case EnvideoEngine_Host:
        case EnvideoEngine_Copy:
        default:
            return false;
    }
}

constexpr inline std::uint32_t engine_to_host1x_class_id(EnvideoEngine engine) {
    switch (engine) {
        case EnvideoEngine_Host:  return HOST1X_CLASS_HOST1X;
        case EnvideoEngine_Nvdec: return HOST1X_CLASS_NVDEC;
        case EnvideoEngine_Nvenc: return HOST1X_CLASS_NVENC;
        case EnvideoEngine_Nvjpg: return HOST1X_CLASS_NVJPG;
        case EnvideoEngine_Vic:   return HOST1X_CLASS_VIC;
        case EnvideoEngine_Ofa:   return HOST1X_CLASS_OFA;
        default:                  return UINT32_C(-1);
    }
}

NvdecVersion get_nvdec_version(int cl);
int get_decode_constraints(EnvideoDevice *device, EnvideoDecodeConstraints *constraints);

} // namespace envid

struct EnvideoDevice:  public envid::Device  { };
struct EnvideoMap:     public envid::Map     { };
struct EnvideoChannel: public envid::Channel { };
struct EnvideoCmdbuf:  public envid::Cmdbuf  { };
