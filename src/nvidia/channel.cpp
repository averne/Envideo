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

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <nv_escape.h>
#include <nv-ioctl.h>
#include <nv-ioctl-numbers.h>
#include <nvos.h>
#include <nvtypes.h>

#include <class/cl0005.h>
#include <class/cl2080.h>
#include <class/clb0b5sw.h>
#include <ctrl/ctrl2080.h>
#include <ctrl/ctrla06f.h>
#include <ctrl/ctrlc36f.h>
#include <clc76f.h>
#include <clc7b5.h>
#include <clc9b0.h>
#include <clc9b7.h>

#include "../cmdbuf.hpp"
#include "../util.hpp"

#include "context.hpp"

namespace envid::nvidia {

namespace {

std::uint32_t get_engine_type(EnvideoEngine engine, std::uint32_t instance) {
    switch (engine) {
        case EnvideoEngine_Host:  return NV2080_ENGINE_TYPE_HOST; // ?
        case EnvideoEngine_Copy:  return NV2080_ENGINE_TYPE_COPY  (instance);
        case EnvideoEngine_Nvdec: return NV2080_ENGINE_TYPE_NVDEC (instance);
        case EnvideoEngine_Nvenc: return NV2080_ENGINE_TYPE_NVENC (instance);
        case EnvideoEngine_Nvjpg: return NV2080_ENGINE_TYPE_NVJPEG(instance);
        case EnvideoEngine_Ofa:   return NV2080_ENGINE_TYPE_OFAn  (instance);
        case EnvideoEngine_Vic:   return NV2080_ENGINE_TYPE_VIC;
        default: return -1;
    }
}

std::uint32_t get_notifier_type(EnvideoEngine engine, std::uint32_t instance) {
    switch (engine) {
        case EnvideoEngine_Copy:  return NV2080_NOTIFIERS_CE    (instance);
        case EnvideoEngine_Nvdec: return NV2080_NOTIFIERS_NVDEC (instance);
        case EnvideoEngine_Nvenc: return NV2080_NOTIFIERS_NVENC (instance);
        case EnvideoEngine_Nvjpg: return NV2080_NOTIFIERS_NVJPEG(instance);
        case EnvideoEngine_Ofa:   return NV2080_NOTIFIERS_OFAn  (instance);
        case EnvideoEngine_Host:  // ?
        case EnvideoEngine_Vic:   // ?
        default: return -1;
    }
}

} // namespace

int Channel::initialize() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    // If we are requested a copy channel, find the first asynchronous engine instance
    std::uint32_t instance = 0;
    if (this->engine == EnvideoEngine_Copy) {
        for (;; ++instance) {
            NV2080_CTRL_CE_GET_CAPS_V2_PARAMS caps = { .ceEngineType = NV2080_ENGINE_TYPE_COPY(instance) };
            ENVID_CHECK(d.nvrm_control(d.subdevice, NV2080_CTRL_CMD_CE_GET_CAPS_V2, caps));
            if (!NV2080_CTRL_CE_GET_CAP(caps.capsTbl, NV2080_CTRL_CE_CAPS_CE_GRCE))
                break;
        }
    }

    this->engine_type   = get_engine_type  (this->engine, instance);
    this->notifier_type = get_notifier_type(this->engine, instance);

    if (this->engine_type == UINT32_C(-1))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    ENVID_CHECK(d.alloc_channel(this->channel_idx, this->engine_type));

    // Reset gpfifo read head tracking
    volatile auto *pbdma_sema = d.get_pbdma_semaphore(this->channel_idx);
    *pbdma_sema = this->gpfifo_pos;

    // Find the class id for the engine
    std::uint32_t cl = 0, gpfifo_cl = d.find_class(0x6f);
    switch (this->engine) {
        case EnvideoEngine_Host:
            cl = gpfifo_cl;
            break;
        case EnvideoEngine_Copy:
        case EnvideoEngine_Nvdec:
        case EnvideoEngine_Nvenc:
        case EnvideoEngine_Nvjpg:
        case EnvideoEngine_Ofa:
        case EnvideoEngine_Vic: {
            ENVID_CHECK(d.get_class_id(this->engine_type, cl));
            break;
        }
        default:
            return ENVIDEO_RC_SYSTEM(EINVAL);
    }

    if (!gpfifo_cl | !cl)
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    // The gpfifo buffer can accomodate 256 command lists before wrapping
    auto gpfifo_size = util::align_up(Channel::num_cmdlists * NVC76F_GP_ENTRY__SIZE, d.page_size);
    ENVID_CHECK(this->entries.initialize(gpfifo_size, d.page_size));

    auto userd_size = util::align_up(sizeof(AmpereAControlGPFifo), d.page_size);
    ENVID_CHECK(this->userd.initialize(userd_size, d.page_size));

    ENVID_CHECK(d.nvrm_alloc(d.device, this->gpfifo, gpfifo_cl, NV_CHANNEL_ALLOC_PARAMS{
        .gpFifoOffset  = this->entries.gpu_addr_pitch,
        .gpFifoEntries = static_cast<std::uint32_t>(Channel::num_cmdlists),
        .hUserdMemory  = this->userd.object.handle,
        .userdOffset   = 0,
        .engineType    = this->engine_type,
    }));

    switch (this->engine) {
        case EnvideoEngine_Host:
            // The host engine was allocated above
            break;
        case EnvideoEngine_Copy:
            ENVID_CHECK(d.nvrm_alloc(this->gpfifo, this->eng, cl, NVB0B5_ALLOCATION_PARAMETERS{
                .version    = NVB0B5_ALLOCATION_PARAMETERS_VERSION_0,
                .engineType = instance,
            }));
            break;
        case EnvideoEngine_Nvdec:
            ENVID_CHECK(d.nvrm_alloc(this->gpfifo, this->eng, cl, NV_BSP_ALLOCATION_PARAMETERS{
                .size           = sizeof(NV_BSP_ALLOCATION_PARAMETERS),
                .engineInstance = instance,
            }));
            break;
        case EnvideoEngine_Nvenc:
            ENVID_CHECK(d.nvrm_alloc(this->gpfifo, this->eng, cl, NV_MSENC_ALLOCATION_PARAMETERS{
                .size           = sizeof(NV_MSENC_ALLOCATION_PARAMETERS),
                .engineInstance = instance,
            }));
            break;
        case EnvideoEngine_Nvjpg:
            ENVID_CHECK(d.nvrm_alloc(this->gpfifo, this->eng, cl, NV_NVJPG_ALLOCATION_PARAMETERS{
                .size           = sizeof(NV_NVJPG_ALLOCATION_PARAMETERS),
                .engineInstance = instance,
            }));
            break;
        case EnvideoEngine_Ofa:
            ENVID_CHECK(d.nvrm_alloc(this->gpfifo, this->eng, cl, NV_OFA_ALLOCATION_PARAMETERS{
                .size           = sizeof(NV_OFA_ALLOCATION_PARAMETERS),
                .engineInstance = instance,
            }));
            break;
        case EnvideoEngine_Vic:
            // Not available?
        default:
            return ENVIDEO_RC_SYSTEM(EINVAL);
    }

    ENVID_CHECK(d.nvrm_control(this->gpfifo, NVA06F_CTRL_CMD_BIND, NVA06F_CTRL_BIND_PARAMS{
        .engineType = this->engine_type,
    }));

    ENVID_CHECK(d.nvrm_control(this->gpfifo, NVA06F_CTRL_CMD_GPFIFO_SCHEDULE, NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS{
        .bEnable     = true,
        .bSkipSubmit = false,
    }));

    NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN_PARAMS work_token_params = {};
    ENVID_CHECK(d.nvrm_control(this->gpfifo, NVC36F_CTRL_CMD_GPFIFO_GET_WORK_SUBMIT_TOKEN, work_token_params));
    this->submit_token = work_token_params.workSubmitToken;

    if (this->notifier_type > 0) {
        // Allocate and bind event to the interrupt
        ENVID_CHECK(d.nvrm_alloc(d.os_event_fd, d.subdevice, this->event, NV01_EVENT_OS_EVENT, NV0005_ALLOC_PARAMETERS{
            .hParentClient = d.root.handle,
            .hClass        = NV01_EVENT_OS_EVENT,
            .notifyIndex   = this->notifier_type | NV01_EVENT_NONSTALL_INTR | NV01_EVENT_WITHOUT_EVENT_DATA,
            .data          = NV_PTR_TO_NvP64(d.os_event_fd),
        }));

        ENVID_CHECK(d.register_event(this->notifier_type));
    }

    return 0;
}

int Channel::finalize() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    d.unregister_event(this->notifier_type);

    this->userd  .finalize();
    this->entries.finalize();

    d.nvrm_free(this->event);
    d.nvrm_free(this->eng);
    d.nvrm_free(this->gpfifo);

    d.free_channel(this->channel_idx);

    return 0;
}

envid::Cmdbuf *Channel::create_cmdbuf() {
    return new envid::GpfifoCmdbuf(false);
}

int Channel::submit(envid::Cmdbuf *cmdbuf, envid::Fence *fence) {
    auto &c = *reinterpret_cast<GpfifoCmdbuf *>(cmdbuf);
    auto &d = *reinterpret_cast<Device *>(this->device);

    // Calculate the gpput value for this submission and this thread,
    // adding to entries for the internal semaphore command lists
    // If necessary, wrap to the start of the gp entry ring buffer
    std::uint32_t gpfifo_pos, prev_gpfifo_pos, num_entries = c.entries.size() + 2;
    if (this->gpfifo_pos + num_entries >= Channel::num_cmdlists - 1)
        prev_gpfifo_pos = 0,                gpfifo_pos = this->gpfifo_pos  = num_entries;
    else
        prev_gpfifo_pos = this->gpfifo_pos, gpfifo_pos = this->gpfifo_pos += num_entries;

    // Assert that the gpget read head is behind our write head (wrapping comparison)
    volatile auto *sema = d.get_pbdma_semaphore(this->channel_idx);
    if (static_cast<std::int8_t>(*sema - prev_gpfifo_pos) > 0)
        return ENVIDEO_RC_SYSTEM(EFAULT);

    auto pbdma_fence        = d.get_pbdma_fence_incr  (this->channel_idx),
         channel_fence      = d.get_channel_fence_incr(this->channel_idx);
    auto pbdma_fence_addr   = fence_id(pbdma_fence)   * sizeof(std::uint32_t),
         channel_fence_addr = fence_id(channel_fence) * sizeof(std::uint32_t);
    auto pbdma_fence_val    = gpfifo_pos,
         channel_fence_val  = fence_value(channel_fence);

    // Insert semaphore increment and interrupt emission, to signal engine completion
    ENVID_CHECK(c.begin(this->engine));
    switch (this->engine) {
        case EnvideoEngine_Host:
            ENVID_CHECK(c.push_reloc(NVC76F_SEM_ADDR_LO, &d.semaphores,
                                     channel_fence_addr, EnvideoRelocType_Default, 0));
            ENVID_CHECK(c.push_value(NVC76F_SEM_PAYLOAD_LO, channel_fence_val));
            ENVID_CHECK(c.push_value(NVC76F_SEM_EXECUTE,
                                     DRF_DEF(C76F, _SEM_EXECUTE, _OPERATION,         _RELEASE) |
                                     DRF_DEF(C76F, _SEM_EXECUTE, _RELEASE_WFI,       _DIS)     |
                                     DRF_DEF(C76F, _SEM_EXECUTE, _PAYLOAD_SIZE,      _32BIT)   |
                                     DRF_DEF(C76F, _SEM_EXECUTE, _RELEASE_TIMESTAMP, _DIS)));
            ENVID_CHECK(c.push_value(NVC76F_NON_STALL_INTERRUPT,
                                     DRF_NUM(C76F, _NON_STALL_INTERRUPT, _HANDLE, 0)));
            break;
        case EnvideoEngine_Copy:
            ENVID_CHECK(c.push_reloc(NVC7B5_SET_SEMAPHORE_A, &d.semaphores,
                                     channel_fence_addr, EnvideoRelocType_Default, 0));
            ENVID_CHECK(c.push_value(NVC7B5_SET_SEMAPHORE_PAYLOAD, channel_fence_val));
            ENVID_CHECK(c.push_value(NVC7B5_LAUNCH_DMA,
                                     DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NONE)                       |
                                     DRF_DEF(C7B5, _LAUNCH_DMA, _SEMAPHORE_TYPE,     _RELEASE_ONE_WORD_SEMAPHORE) |
                                     DRF_DEF(C7B5, _LAUNCH_DMA, _INTERRUPT_TYPE,     _NON_BLOCKING)));
            break;
        case EnvideoEngine_Nvdec:
            ENVID_CHECK(c.push_reloc(NVC9B0_SEMAPHORE_A, &d.semaphores,
                                     channel_fence_addr, EnvideoRelocType_Default, 0));
            ENVID_CHECK(c.push_value(NVC9B0_SEMAPHORE_C, channel_fence_val));
            ENVID_CHECK(c.push_value(NVC9B0_SEMAPHORE_D,
                                     DRF_DEF(C9B0, _SEMAPHORE_D, _OPERATION,      _RELEASE) |
                                     DRF_DEF(C9B0, _SEMAPHORE_D, _STRUCTURE_SIZE, _ONE)     |
                                     DRF_DEF(C9B0, _SEMAPHORE_D, _PAYLOAD_SIZE,   _32BIT)));
            ENVID_CHECK(c.push_value(NVC9B0_SEMAPHORE_D,
                                     DRF_DEF(C9B0, _SEMAPHORE_D, _OPERATION,      _TRAP)));
            break;
        case EnvideoEngine_Nvenc:
            ENVID_CHECK(c.push_reloc(NVC9B7_SEMAPHORE_A, &d.semaphores,
                                     channel_fence_addr, EnvideoRelocType_Default, 0));
            ENVID_CHECK(c.push_value(NVC9B7_SEMAPHORE_C, channel_fence_val));
            ENVID_CHECK(c.push_value(NVC9B7_SEMAPHORE_D,
                                     DRF_DEF(C9B7, _SEMAPHORE_D, _OPERATION,      _RELEASE) |
                                     DRF_DEF(C9B7, _SEMAPHORE_D, _STRUCTURE_SIZE, _ONE)     |
                                     DRF_DEF(C9B7, _SEMAPHORE_D, _PAYLOAD_SIZE,   _32BIT)));
            ENVID_CHECK(c.push_value(NVC9B7_SEMAPHORE_D,
                                     DRF_DEF(C9B7, _SEMAPHORE_D, _OPERATION,      _TRAP)));
            break;
        case EnvideoEngine_Nvjpg:
        case EnvideoEngine_Ofa:
            // TODO
        case EnvideoEngine_Vic:
        default:
            return ENVIDEO_RC_SYSTEM(EINVAL);
    }
    ENVID_CHECK(c.end());

    // Insert a second semaphore write mirroring the gpget read head, to signal fetching completion
    // Unlike other engines, this takes addresses in litte-endian format, so we can't use the push_reloc helper (epic)
    auto addr = d.semaphores.gpu_addr_pitch + pbdma_fence_addr;
    ENVID_CHECK(c.begin(EnvideoEngine_Host));
    ENVID_CHECK(c.push_value(NVC76F_SEM_ADDR_LO,    addr >> 0 ));
    ENVID_CHECK(c.push_value(NVC76F_SEM_ADDR_HI,    addr >> 32));
    ENVID_CHECK(c.push_value(NVC76F_SEM_PAYLOAD_LO, pbdma_fence_val));
    ENVID_CHECK(c.push_value(NVC76F_SEM_EXECUTE,
                             DRF_DEF(C76F, _SEM_EXECUTE, _OPERATION,         _RELEASE) |
                             DRF_DEF(C76F, _SEM_EXECUTE, _RELEASE_WFI,       _DIS)     |
                             DRF_DEF(C76F, _SEM_EXECUTE, _PAYLOAD_SIZE,      _32BIT)   |
                             DRF_DEF(C76F, _SEM_EXECUTE, _RELEASE_TIMESTAMP, _DIS)));
    ENVID_CHECK(c.end());

    auto *pb = static_cast<std::uint64_t *>(this->entries.cpu_addr);
    std::ranges::copy(c.entries, pb + prev_gpfifo_pos);

    volatile auto *control = reinterpret_cast<AmpereAControlGPFifo *>(this->userd.cpu_addr);
    control->GPPut = gpfifo_pos;

    d.kickoff(this->submit_token);
    util::write_fence();

    *fence = channel_fence;

    return 0;
}

int Channel::get_clock_rate(std::uint32_t &clock) {
    if (!engine_is_multimedia(this->engine))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    auto &d = *reinterpret_cast<Device *>(this->device);

    RUSD_CLK_PUBLIC_DOMAIN_INFOS clk_info = {};
    ENVID_CHECK(d.read_clocks(clk_info));

    // XXX: Can we get per-engine frequency information?
    clock = clk_info.info[RUSD_CLK_PUBLIC_DOMAIN_VIDEO].targetClkMHz * 1'000'000;

    return 0;
}

int Channel::set_clock_rate(std::uint32_t clock) {
    if (!engine_is_multimedia(this->engine))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    // XXX: Not possible?
    return 0;
}

} // namespace envid::nvidia
