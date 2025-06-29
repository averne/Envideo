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
        int map_gpu(int flags = 0);
        int unmap_cpu();
        int unmap_gpu();

    public:
        int fd              = 0;
        std::uint32_t gem   = 0;
        void *cache_op_addr = nullptr;

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
        std::uint32_t handle    = 0,
                      module_id = 0,
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
        int map_buffer  (Map &map, std::uint64_t &addr, int flags, bool cacheable, bool pitch);
        int unmap_buffer(Map &map, std::uint64_t &addr);

        int drm_open_channel(std::uint32_t &handle, std::uint32_t cl);
        int drm_close_channel(std::uint32_t handle);
        int drm_alloc_syncpt(std::uint32_t &id);
        int drm_free_syncpt(std::uint32_t id);
        int drm_channel_map(std::uint32_t channel_handle, std::uint32_t map_handle, std::uint32_t &id);
        int drm_channel_unmap(std::uint32_t channel_handle, std::uint32_t id);

        int drm_fd_to_handle(int fd, std::uint32_t &gem);
        int drm_close_gem(std::uint32_t gem);

    private:
        int get_characteristics(nvgpu_gpu_characteristics &characteristics) const;
        int alloc_as(std::uint32_t big_page_size);
        int open_tsg();
        int query_syncpt_map_params();
        int free_as();
        int close_tsg();

    public:
        bool has_tegra_drm = false;

        int chip_id       = 0;
        int nvhost_fd     = 0,
            nvhost_gpu_fd = 0,
            nvmap_fd      = 0,
            nvas_fd       = 0,
            nvtsg_fd      = 0;

        std::uint16_t host1x_version = 0,
                      bl_kind        = 0;

        std::uint32_t copy_class = 0;

        std::uint64_t syncpt_va_base   = 0;
        std::uint32_t syncpt_page_size = 0;

#if defined(__SWITCH__)
        NvAddressSpace gpu_as   = {};
#endif
};

} // namespace envid::nvgpu
