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

#include <algorithm>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <nvmisc.h>
#include <clc76f.h>

#include <nvgpu.h>
#include <nvhost_ioctl.h>

#include "context.hpp"
#include "../cmdbuf.hpp"

namespace envid::nvgpu {

#if defined(__SWITCH__)
MmuModuleId get_mmu_id(EnvideoEngine engine) {
    switch (engine) {
        // Values are flipped in the libnx enum (TODO fix this eventually)
        case EnvideoEngine_Nvdec: return static_cast<MmuModuleId>(5);
        case EnvideoEngine_Nvenc: return static_cast<MmuModuleId>(6);
        case EnvideoEngine_Nvjpg: return MmuModuleId_Nvjpg;
        default:                  return static_cast<MmuModuleId>(-1);
    }
}
#endif

int Channel::get_syncpoint(std::uint32_t &syncpt) const {
#if defined(__linux__)
    auto args = nvhost_get_param_arg{};
    ENVID_CHECK_ERRNO(::ioctl(this->fd, NVHOST_IOCTL_CHANNEL_GET_SYNCPOINT, &args));

    syncpt = args.value;
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvioctlChannel_GetSyncpt(this->fd, 0, &syncpt));
#endif

    return 0;
}

int Channel::set_submit_timeout(std::uint32_t timeout_ms) const {
#if defined(__linux__)
    auto args = nvhost_set_timeout_args{
        .timeout = timeout_ms,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->fd, NVHOST_IOCTL_CHANNEL_SET_TIMEOUT, &args));
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvioctlChannel_SetSubmitTimeout(this->fd, timeout_ms));
#endif

    return 0;
}

int Channel::set_nvmap_fd(Device &device) const {
#if defined(__linux__)
    auto args = nvgpu_set_nvmap_fd_args{
        .fd = static_cast<std::uint32_t>(device.nvmap_fd),
    };
    ENVID_CHECK_ERRNO(::ioctl(this->fd, NVGPU_IOCTL_CHANNEL_SET_NVMAP_FD, &args));
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(nvioctlChannel_SetNvmapFd(this->fd, static_cast<std::uint32_t>(device.nvmap_fd)));
#endif

    return 0;
}

int Channel::setup_bind(std::uint32_t num_gpfifo_entries) const {
#if defined(__linux__)
    auto args = nvgpu_channel_setup_bind_args{
        .num_gpfifo_entries = num_gpfifo_entries,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->fd, NVGPU_IOCTL_CHANNEL_SETUP_BIND, &args));
#endif

    return 0;
}

int Channel::alloc_obj_ctx(std::uint64_t &obj_id, std::uint32_t class_num) const {
#if defined(__linux__)
    auto args = nvgpu_alloc_obj_ctx_args{
        .class_num = class_num,
    };
    ENVID_CHECK_ERRNO(ioctl(this->fd, NVGPU_IOCTL_CHANNEL_ALLOC_OBJ_CTX, &args));

    obj_id = args.obj_id;
#endif

    return 0;
}

int Channel::initialize() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    if (this->engine != EnvideoEngine_Copy) {
        this->type = Type::Host1x;

        std::string_view path;
        switch (this->engine) {
            case EnvideoEngine_Nvdec:
                this->module_id = NVHOST_MODULE_NVDEC;
                path = "/dev/nvhost-nvdec";
                break;
            case EnvideoEngine_Nvenc:
                this->module_id = NVHOST_MODULE_MSENC;
                path = "/dev/nvhost-msenc";
                break;
            case EnvideoEngine_Nvjpg:
                this->module_id = NVHOST_MODULE_NVJPG;
                path = "/dev/nvhost-nvjpg";
                break;
            case EnvideoEngine_Ofa:
                // TODO: this->module_id = NVHOST_MODULE_OFA;
                path = "/dev/nvhost-ofa";
                break;
            case EnvideoEngine_Vic:
                this->module_id = NVHOST_MODULE_VIC;
                path = "/dev/nvhost-vic";
                break;
            default:
                return ENVIDEO_RC_SYSTEM(EINVAL);
        }

#if defined(__linux__)
        ENVID_CHECK_ERRNO(this->fd = ::open(path.data(), O_RDWR | O_SYNC | O_CLOEXEC));
#elif defined(__SWITCH__)
        ENVID_CHECK_RC(nvChannelCreate(&this->channel, path.data()));

        if (auto id = get_mmu_id(this->engine); id <= MmuModuleId_Nvjpg) {
            ENVID_CHECK_RC(mmuRequestInitialize(&this->mmu_request, id, 8, false));
        }

        this->fd = this->channel.fd;
#endif

        ENVID_CHECK(this->get_syncpoint(this->syncpt));
        ENVID_CHECK(this->set_submit_timeout(1000));
        ENVID_CHECK(this->set_clock_rate(UINT32_MAX));
    } else {
        this->type = Type::Gpfifo;

#if defined(__linux__)
        ENVID_CHECK(d.open_gpu_channel(*this));
        ENVID_CHECK(this->set_nvmap_fd(d));
        ENVID_CHECK(d.bind_channel_as(*this));
        ENVID_CHECK(d.bind_channel_tsg(*this));
        ENVID_CHECK(this->setup_bind(GpfifoCmdbuf::num_entries << 2));
        ENVID_CHECK(this->alloc_obj_ctx(this->obj_id, d.copy_class));
#elif defined(__SWITCH__)
        ENVID_CHECK_RC(nvChannelCreate(&this->channel, "/dev/nvhost-gpu"));

        this->fd = this->channel.fd;

        ENVID_CHECK_RC(nvioctlNvhostAsGpu_BindChannel(d.gpu_as.fd, this->fd));

        ENVID_CHECK_RC(nvioctlChannel_AllocGpfifoEx2(this->fd, GpfifoCmdbuf::num_entries, 1, 0, 0, 0, 0, nullptr));

        ENVID_CHECK_RC(nvioctlChannel_AllocObjCtx(this->fd, d.copy_class, 0, nullptr));
#endif
    }

    return 0;
}

int Channel::finalize() {
#if defined(__linux__)
    if (this->fd)
        ::close(this->fd);
#elif defined(__SWITCH__)
    nvChannelClose(&this->channel);
    if (this->mmu_request.id)
        mmuRequestFinalize(&this->mmu_request);
#endif

    return 0;
}

envid::Cmdbuf *Channel::create_cmdbuf() {
    if (engine_is_multimedia(this->engine)) {
        bool need_setclass =
#if defined(__linux__)
            false;
#elif defined(__SWITCH__)
            true;
#endif

        return new envid::Host1xCmdbuf(need_setclass);
    } else {
        return new envid::GpfifoCmdbuf(true);
    }
}

int Channel::submit(envid::Cmdbuf *cmdbuf, envid::Fence *fence) {
    if (this->engine != EnvideoEngine_Copy) {
        auto *c = reinterpret_cast<Host1xCmdbuf *>(cmdbuf);

        // Insert syncpt increment in a new command list
        c->begin(this->engine);
        c->add_syncpt_incr(this->syncpt);
        c->end();

#if defined(__linux__)
        auto args = nvhost_submit_args{
            .submit_version          = NVHOST_SUBMIT_VERSION_V2,
            .num_syncpt_incrs        = static_cast<std::uint32_t>(c->syncpt_incrs.size()),
            .num_cmdbufs             = static_cast<std::uint32_t>(c->cmdbufs     .size()),
            .num_relocs              = static_cast<std::uint32_t>(c->relocs      .size()),
            .timeout                 = 0,
            .flags                   = 0,
            .fence                   = 0,
            .syncpt_incrs            = reinterpret_cast<std::uintptr_t>(c->syncpt_incrs.data()),
            .cmdbuf_exts             = reinterpret_cast<std::uintptr_t>(c->cmdbuf_exts .data()),
            .reloc_types             = reinterpret_cast<std::uintptr_t>(c->reloc_types .data()),
            .cmdbufs                 = reinterpret_cast<std::uintptr_t>(c->cmdbufs     .data()),
            .relocs                  = reinterpret_cast<std::uintptr_t>(c->relocs      .data()),
            .reloc_shifts            = reinterpret_cast<std::uintptr_t>(c->reloc_shifts.data()),
            .class_ids               = reinterpret_cast<std::uintptr_t>(c->class_ids   .data()),
            .fences                  = reinterpret_cast<std::uintptr_t>(c->fences      .data()),
        };
        ENVID_CHECK_ERRNO(::ioctl(this->fd, NVHOST_IOCTL_CHANNEL_SUBMIT, &args));

        auto fence_val = args.fence;
#elif defined(__SWITCH__)
        std::uint32_t num_incrs = 0;
        std::array<nvioctl_syncpt_incr, 32> incrs;
        for (; num_incrs < std::min(c->syncpt_incrs.size(), incrs.size()); ++num_incrs) {
            incrs[num_incrs] = nvioctl_syncpt_incr{
                .syncpt_id    = c->syncpt_incrs[num_incrs].syncpt_id,
                .syncpt_incrs = c->syncpt_incrs[num_incrs].syncpt_incrs,
                .waitbase_id  = UINT32_C(-1),
                .next         = UINT32_C(-1),
                .prev         = UINT32_C(-1),
            };
        }

        nvioctl_fence f;
        if (auto rc = nvioctlChannel_Submit(this->fd,
                                            reinterpret_cast<nvioctl_cmdbuf *>(c->cmdbufs.data()), c->cmdbufs.size(),
                                            nullptr, nullptr, 0,
                                            incrs.data(), num_incrs, &f, 1);
                R_FAILED(rc))
            return ENVIDEO_RC_SYSTEM(rc);

        auto fence_val = f.value;
#endif

        *fence = make_fence(this->syncpt, fence_val);
    } else {
        auto *c = reinterpret_cast<GpfifoCmdbuf *>(cmdbuf);

#if defined(__linux__)
        auto args = nvgpu_submit_gpfifo_args{
            .gpfifo      = reinterpret_cast<std::uintptr_t>(c->entries.data()),
            .num_entries = static_cast<std::uint32_t>(c->entries.size()),
            .flags       = NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET,
        };
        ENVID_CHECK_ERRNO(::ioctl(this->fd, NVGPU_IOCTL_CHANNEL_SUBMIT_GPFIFO, &args));

        *fence = make_fence(args.fence.id, args.fence.value);
#elif defined(__SWITCH__)
        nvioctl_fence f;
        auto flags = NVGPU_SUBMIT_GPFIFO_FLAGS_FENCE_GET | NVGPU_SUBMIT_GPFIFO_FLAGS_HW_FORMAT;
        if (auto rc = nvioctlChannel_SubmitGpfifo(this->fd, reinterpret_cast<nvioctl_gpfifo_entry *>(c->entries.data()),
                                                  c->entries.size(), flags, &f);
                R_FAILED(rc))
            return ENVIDEO_RC_SYSTEM(rc);

        *fence = make_fence(f.id, f.value);
#endif
    }

    return 0;
}

int Channel::get_clock_rate(std::uint32_t &clock) {
    if (!engine_is_multimedia(this->engine))
        return ENVIDEO_RC_SYSTEM(EINVAL);

#if defined(__linux__)
    auto args = nvhost_clk_rate_args{
        .moduleid = this->module_id,
    };
    ENVID_CHECK_ERRNO(ioctl(this->fd, NVHOST_IOCTL_CHANNEL_GET_CLK_RATE, &args));

    clock = args.rate;
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(mmuRequestGet(&this->mmu_request, &clock));
#endif

    return 0;
}

int Channel::set_clock_rate(std::uint32_t clock) {
    if (!engine_is_multimedia(this->engine))
        return ENVIDEO_RC_SYSTEM(EINVAL);

#if defined(__linux__)
    auto args = nvhost_clk_rate_args{
        .rate     = clock,
        .moduleid = this->module_id,
    };
    ENVID_CHECK_ERRNO(::ioctl(this->fd, NVHOST_IOCTL_CHANNEL_SET_CLK_RATE, &args));
#elif defined(__SWITCH__)
    ENVID_CHECK_RC(mmuRequestSetAndWait(&this->mmu_request, clock, -1));
#endif

    return 0;
}

} // namespace envid::nvgpu
