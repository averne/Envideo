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
#include <array>
#include <string_view>

#include <envideo.h>

#include <nvgpu.h>

#include "../common.hpp"

namespace envid::nvgpu {

class Map;
class Channel;
class Device;

class Map final: public envid::Map {
    public:
        Map(envid::Device *device, EnvideoMapFlags flags): envid::Map(device, flags) { }
        virtual int initialize(std::size_t size, std::size_t align)                override;
        virtual int initialize(void *address, std::size_t size, std::size_t align) override;
        virtual int finalize()                                                     override;
        virtual int pin(envid::Channel *channel)                                   override;
        virtual int cache_op(std::size_t offset, std::size_t len,
                             EnvideoCacheFlags flags)                              override;

    private:
        int get_fd();
        int map_cpu();
        int map_gpu();
        int unmap_cpu();
        int unmap_gpu();

    public:
        int fd = 0;

#if defined(__SWITCH__)
        void *alloc = nullptr;
        NvMap map = {};
#endif
};

class Channel final: public envid::Channel {
    public:
        Channel(envid::Device *device, EnvideoEngine engine): envid::Channel(device, engine) { }
        virtual int            initialize()                                       override;
        virtual int            finalize()                                         override;
        virtual envid::Cmdbuf *create_cmdbuf()                                    override;
        virtual int            submit(envid::Cmdbuf *cmdbuf, envid::Fence *fence) override;
        virtual int            get_clock_rate(std::uint32_t &clock)               override;
        virtual int            set_clock_rate(std::uint32_t clock)                override;

    private:
        int get_syncpoint(std::uint32_t &syncpt)         const;
        int set_submit_timeout(std::uint32_t timeout_ms) const;

        int set_nvmap_fd(Device &device) const;
        int setup_bind(std::uint32_t num_gpfifo_entries) const;
        int alloc_obj_ctx(std::uint64_t &obj_id, std::uint32_t class_num) const;

    public:
        int           fd        = 0;
        std::uint32_t module_id = 0,
                      syncpt    = 0;
        std::uint64_t obj_id    = 0;

#if defined(__SWITCH__)
        NvChannel  channel     = {};
        MmuRequest mmu_request = {};
#endif
};

class Device: public envid::Device {
    public:
        static bool probe();

        virtual int initialize()                                       override;
        virtual int finalize()                                         override;
        virtual int wait(envid::Fence fence, std::uint64_t timeout_us) override;
        virtual int poll(envid::Fence fence, bool &is_done)            override;

        virtual envid::Map *get_semaphore_map() const override {
            return nullptr;
        }

    public:
        int open_gpu_channel(Channel &channel) const;
        int bind_channel_as (Channel &channel) const;
        int bind_channel_tsg(Channel &channel) const;
        int map_buffer  (Map &map, std::uint64_t &addr, bool cacheable, bool pitch);
        int unmap_buffer(Map &map, std::uint64_t &addr);

    private:
        int get_characteristics(nvgpu_gpu_characteristics &characteristics) const;
        int alloc_as(std::uint32_t big_page_size);
        int open_tsg();
        int free_as();
        int close_tsg();

    public:
        int chip_id       = 0;
        int nvhost_fd     = 0,
            nvhost_gpu_fd = 0,
            nvmap_fd      = 0,
            nvas_fd       = 0,
            nvtsg_fd      = 0;

        std::uint32_t copy_class = 0;

#if defined(__SWITCH__)
        NvAddressSpace gpu_as   = {};
#endif
};

} // namespace envid::nvgpu
