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
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <numeric>
#include <string_view>
#include <vector>

#include <envideo.h>

#include <class/cl00de.h>

#include "../common.hpp"

namespace envid::nvidia {

struct Object {
    std::uint32_t handle, parent;
};

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

    public:
        int map_cpu(bool system = false);
        int unmap_cpu();
        int map_gpu();
        int unmap_gpu();

    public:
        Object        object         = {};
        std::uint64_t linear_address = 0;
};

class Channel final: public envid::Channel {
    public:
        // Number of gpfifo entries in our pbdma
        // This must be one plus the the maximum value of an unsigned integer type
        constexpr static auto num_cmdlists = UINT8_MAX + 1;

    public:
        Channel(envid::Device *device, EnvideoEngine engine):
            envid::Channel(device, engine),
            userd  (device, static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuUncacheable | EnvideoMap_UsageGeneric)),
            entries(device, static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuUncacheable | EnvideoMap_UsageCmdbuf)) { }

        virtual int            initialize()                                       override;
        virtual int            finalize()                                         override;
        virtual envid::Cmdbuf *create_cmdbuf()                                    override;
        virtual int            submit(envid::Cmdbuf *cmdbuf, envid::Fence *fence) override;
        virtual int            get_clock_rate(std::uint32_t &clock)               override;
        virtual int            set_clock_rate(std::uint32_t clock)                override;

    public:
        int channel_idx = -1;

        Object gpfifo = {}, eng = {}, event = {};
        Map userd, entries;

        std::uint32_t engine_type  = -1, notifier_type = -1;
        std::uint32_t submit_token = 0,  gpfifo_pos    = 0;
};

class Device final: public envid::Device {
    public:
        constexpr static auto sema_map_size = 0x1000;
        constexpr static auto num_queues    = Device::sema_map_size / sizeof(std::uint32_t) / 2;

        using channels_mask_type = std::uint64_t;
        constexpr static auto channel_mask_bitwidth = std::numeric_limits<Device::channels_mask_type>::digits;

    public:
        static bool probe();

        Device():
            rusd      (this, EnvideoMap_CpuWriteCombine),
            usermode  (this, EnvideoMap_CpuWriteCombine),
            semaphores(this, static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuUncacheable | EnvideoMap_UsageGeneric)) {}

        virtual int initialize()                                       override;
        virtual int finalize()                                         override;
        virtual int wait(envid::Fence fence, std::uint64_t timeout_us) override;
        virtual int poll(envid::Fence fence, bool &is_done)            override;

        virtual const envid::Map *get_semaphore_map() const override {
            return &this->semaphores;
        }

    public:
        int alloc_channel(int &idx, std::uint32_t engine_type);
        int free_channel (int  idx);
        int register_event  (std::uint32_t notifier_type);
        int unregister_event(std::uint32_t notifier_type);

        bool check_channel_idx(int idx) {
            return !!(this->channels_mask[(idx - 1) / channel_mask_bitwidth] & (UINT64_C(1) << ((idx - 1) & (channel_mask_bitwidth - 1))));
        }

        std::uint32_t get_pbdma_fence_id(int idx) const {
            return (idx - 1) * 2 + 0;
        }

        std::uint32_t get_channel_fence_id(int idx) const {
            return (idx - 1) * 2 + 1;
        }

        envid::Fence get_pbdma_fence_incr(int idx) {
            auto id = this->get_pbdma_fence_id(idx);
            return make_fence(id, ++this->fence_values[id]);
        }

        envid::Fence get_channel_fence_incr(int idx) {
            auto id = this->get_channel_fence_id(idx);
            return make_fence(id, ++this->fence_values[id]);
        }

        volatile std::uint32_t *get_pbdma_semaphore(int idx) const {
            return &static_cast<std::uint32_t *>(this->semaphores.cpu_addr)[(idx - 1) * 2 + 0];
        }

        volatile std::uint32_t *get_channel_semaphore(int idx) const {
            return &static_cast<std::uint32_t *>(this->semaphores.cpu_addr)[(idx - 1) * 2 + 1];
        }

        std::uint32_t find_class(std::uint32_t target) const {
            auto cl = std::ranges::find_if(this->classes, [&target](auto c) { return (c & 0xff) == target; });
            return (cl == this->classes.end()) ? 0 : *cl;
        }

        bool poll_internal(envid::Fence fence) const;
        int get_class_id(std::uint32_t engine_type, std::uint32_t &cl) const;
        int read_clocks(RUSD_CLK_PUBLIC_DOMAIN_INFOS &clk_info, bool update = false) const;
        void kickoff(std::uint32_t token) const;

        int nvrm_alloc(int fd, const Object &parent, Object &obj, std::uint32_t cl,
                       void *params = nullptr, std::uint32_t params_size = 0) const;
        int nvrm_free(int fd, Object &obj) const;
        int nvrm_control(int fd, const Object &obj, std::uint32_t cmd,
                         void *params = nullptr, std::uint32_t params_size = 0) const;

        template <typename T = std::nullptr_t>
        int nvrm_alloc(int fd, const Object &parent, Object &obj, std::uint32_t cl, T &&params = {}) const {
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return this->nvrm_alloc(fd, parent, obj, cl, nullptr, 0);
            else
                return this->nvrm_alloc(fd, parent, obj, cl, &params, sizeof(params));
        }

        template <typename T = std::nullptr_t>
        int nvrm_alloc(const Object &parent, Object &obj, std::uint32_t cl, T &&params = {}) const {
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return this->nvrm_alloc(this->ctl_fd, parent, obj, cl, nullptr, 0);
            else
                return this->nvrm_alloc(this->ctl_fd, parent, obj, cl, &params, sizeof(params));
        }

        int nvrm_free(Object &obj) const {
            return this->nvrm_free(this->ctl_fd, obj);
        }

        template <typename T = std::nullptr_t>
        int nvrm_control(int fd, const Object &obj, std::uint32_t cmd, T &&params = {}) const {
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return this->nvrm_control(fd, obj, cmd, nullptr, 0);
            else
                return this->nvrm_control(fd, obj, cmd, &params, sizeof(params));
        }

        template <typename T = std::nullptr_t>
        int nvrm_control(const Object &obj, std::uint32_t cmd, T &&params = {}) const {
            if constexpr (std::is_same_v<T, std::nullptr_t>)
                return this->nvrm_control(this->ctl_fd, obj, cmd, nullptr, 0);
            else
                return this->nvrm_control(this->ctl_fd, obj, cmd, &params, sizeof(params));
        }

    public:
        std::array<char, 32>           card_path  = {};
        std::array<std::uint8_t, 0x10> card_uuid  = {};
        std::vector<std::uint32_t>     classes    = {};
        std::vector<std::uint32_t>     engines    = {};

        int ctl_fd = 0, card_fd = 0;

        Object root = {}, device       = {}, subdevice    = {},
            vaspace = {}, pitch_ctxdma = {}, block_ctxdma = {};
        Map rusd, usermode, semaphores;

        int os_event_fd = 0;
        Object os_event = {};
        util::FlatHashMap<std::uint32_t, std::uint32_t> event_refs = {};

        std::array<Device::channels_mask_type, Device::num_queues / Device::channel_mask_bitwidth> channels_mask = {};
        std::array<std::atomic_uint32_t, Device::num_queues * 2> fence_values = {};
        static_assert(decltype(Device::fence_values)::value_type::is_always_lock_free);
};

} // namespace envid::nvidia
