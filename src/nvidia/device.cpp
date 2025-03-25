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
#include <array>
#include <algorithm>
#include <chrono>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <nvos.h>
#include <nvtypes.h>
#include <nv_escape.h>
#include <nv-ioctl.h>
#include <nv-ioctl-numbers.h>
#include <nv-unix-nvos-params-wrappers.h>

#define PORT_IS_KERNEL_BUILD  0
#define PORT_IS_CHECKED_BUILD 0
#define PORT_MODULE_atomic    1
#include <nvport/nvport.h>
#define portMemCopy(d, ld, s, ls) memcpy(d, s, ld)
#define portMemSet(d, v, l)       memset(d, v, l)

#define SDK_ALL_CLASSES_INCLUDE_FULL_HEADER
#include <g_allclasses.h>
#include <ctrl/ctrl0080.h>
#include <ctrl/ctrl2080.h>
#include <ctrl/ctrl00de.h>
#include <ctrl/ctrl0000/ctrl0000gpu.h>
#include <ctrl/ctrl0000/ctrl0000unix.h>
#include <ctrl/ctrl0000/ctrl0000system.h>

#include "../util.hpp"

#include "context.hpp"

namespace envid::nvidia {

namespace {

template <typename T> int nvesc_ioctl(int fd, int dir, int nr, T *type) {
    return ::ioctl(fd, _IOC(dir, NV_IOCTL_MAGIC, nr, sizeof(T)), type);
}

template <typename T> int nvesc_io(int fd, int nr, T *type) {
    return nvesc_ioctl(fd, _IOC_NONE, nr, type);
}

template <typename T> int nvesc_ior(int fd, int nr, T *type) {
    return nvesc_ioctl(fd, _IOC_READ, nr, type);
}

template <typename T> int nvesc_iow(int fd, int nr, T *type) {
    return nvesc_ioctl(fd, _IOC_WRITE, nr, type);
}

template <typename T> int nvesc_iowr(int fd, int nr, T *type) {
    return nvesc_ioctl(fd, _IOC_READ | _IOC_WRITE, nr, type);
}

std::uint32_t get_memory_class(EnvideoMapFlags flags) {
    switch (ENVIDEO_MAP_GET_LOCATION_FLAGS(flags)) {
        case EnvideoMap_LocationHost:
            return NV01_MEMORY_SYSTEM;
        case EnvideoMap_LocationDevice:
            return NV01_MEMORY_LOCAL_USER;
        default:
            return -1;
    }
}

std::uint32_t get_memory_type(EnvideoMapFlags flags) {
    switch (ENVIDEO_MAP_GET_USAGE_FLAGS(flags)) {
        case EnvideoMap_UsageGeneric:
        case EnvideoMap_UsageFramebuffer:
        case EnvideoMap_UsageEngine:
        case EnvideoMap_UsageCmdbuf:
            return NVOS32_TYPE_IMAGE;
        default:
            return -1;
    }
}

std::uint32_t get_alloc_flags(EnvideoMapFlags flags, std::uint32_t *attr, std::uint32_t *attr2, bool from_va = false) {
    std::uint32_t cpu_cache_flags, gpu_cache_flags, location_flags;

    switch (ENVIDEO_MAP_GET_CPU_FLAGS(flags)) {
        case EnvideoMap_CpuCacheable:
            cpu_cache_flags = DRF_DEF(OS32, _ATTR, _COHERENCY, _CACHED);
            break;
        case EnvideoMap_CpuWriteCombine:
            cpu_cache_flags = DRF_DEF(OS32, _ATTR, _COHERENCY, _WRITE_COMBINE);
            break;
        case EnvideoMap_CpuUncacheable:
        case EnvideoMap_CpuUnmapped:
            cpu_cache_flags = DRF_DEF(OS32, _ATTR, _COHERENCY, _UNCACHED);
            break;
        default:
            return -1;
    }

    switch (ENVIDEO_MAP_GET_GPU_FLAGS(flags)) {
        case EnvideoMap_GpuCacheable:
            gpu_cache_flags = DRF_DEF(OS32, _ATTR2, _GPU_CACHEABLE, _YES);
            break;
        case EnvideoMap_GpuUncacheable:
        case EnvideoMap_GpuUnmapped:
            gpu_cache_flags = DRF_DEF(OS32, _ATTR2, _GPU_CACHEABLE, _NO);
            break;
        default:
            return -1;
    }

    switch (ENVIDEO_MAP_GET_LOCATION_FLAGS(flags)) {
        case EnvideoMap_LocationHost:
            location_flags = DRF_DEF(OS32, _ATTR, _LOCATION,    _PCI);
            break;
        case EnvideoMap_LocationDevice:
            location_flags = DRF_DEF(OS32, _ATTR, _LOCATION,    _VIDMEM);
            break;
        default:
            return -1;
    }

    std::uint32_t res = 0;
    switch (ENVIDEO_MAP_GET_USAGE_FLAGS(flags)) {
        case EnvideoMap_UsageGeneric:
            *attr  = DRF_DEF(OS32, _ATTR, _PAGE_SIZE, _4KB) | DRF_DEF(OS32, _ATTR, _PHYSICALITY, _CONTIGUOUS) |
                     cpu_cache_flags | location_flags;
            *attr2 = DRF_DEF(OS32, _ATTR2, _ZBC, _PREFER_NO_ZBC) | gpu_cache_flags;
            res    = NVOS32_ALLOC_FLAGS_PERSISTENT_VIDMEM;
            break;
        case EnvideoMap_UsageFramebuffer:
            if (ENVIDEO_MAP_GET_LOCATION_FLAGS(flags) == EnvideoMap_LocationDevice)
                *attr = DRF_DEF(OS32, _ATTR, _PAGE_SIZE, _HUGE);
            else
                *attr = DRF_DEF(OS32, _ATTR, _PAGE_SIZE, _DEFAULT);

            *attr |= DRF_DEF(OS32, _ATTR, _PHYSICALITY, _NONCONTIGUOUS) | cpu_cache_flags | location_flags;
            *attr2 = DRF_DEF(OS32, _ATTR2, _ZBC, _PREFER_NO_ZBC) | DRF_DEF(OS32, _ATTR2, _PAGE_SIZE_HUGE, _DEFAULT) | gpu_cache_flags;
            res    = NVOS32_ALLOC_FLAGS_PERSISTENT_VIDMEM;
            break;
        case EnvideoMap_UsageEngine:
            *attr  = DRF_DEF(OS32, _ATTR, _PAGE_SIZE, _DEFAULT) | DRF_DEF(OS32, _ATTR, _PHYSICALITY, _NONCONTIGUOUS) |
                     cpu_cache_flags | location_flags;
            *attr2 = DRF_DEF(OS32, _ATTR2, _ZBC, _PREFER_NO_ZBC) | gpu_cache_flags;
            res    = NVOS32_ALLOC_FLAGS_PERSISTENT_VIDMEM;
            break;
        case EnvideoMap_UsageCmdbuf:
            *attr  = DRF_DEF(OS32, _ATTR, _PAGE_SIZE, _4KB) | DRF_DEF(OS32, _ATTR, _PHYSICALITY, _NONCONTIGUOUS) |
                     cpu_cache_flags | location_flags;
            *attr2 = DRF_DEF(OS32, _ATTR2, _ZBC, _PREFER_NO_ZBC) | gpu_cache_flags;
            break;
        default:
            return -1;
    }

    // Fixup flags if mapping preallocated memory (host heap memory)
    if (from_va) {
        *attr = FLD_SET_DRF(OS32, _ATTR, _LOCATION,    _PCI,           *attr);
        *attr = FLD_SET_DRF(OS32, _ATTR, _PAGE_SIZE,   _DEFAULT,       *attr);
        *attr = FLD_SET_DRF(OS32, _ATTR, _PHYSICALITY, _NONCONTIGUOUS, *attr);

        // The only two possible attributes are cached and writeback cached (see osCreateOsDescriptorFromPageArray)
        if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) != EnvideoMap_CpuCacheable)
            *attr = FLD_SET_DRF(OS32, _ATTR, _COHERENCY, _WRITE_BACK, *attr);
    }

    return res | NVOS32_ALLOC_FLAGS_ALIGNMENT_FORCE | NVOS32_ALLOC_FLAGS_MAP_NOT_REQUIRED;
}

} // namespace

int Device::nvrm_alloc(int fd, const Object &parent, Object &obj, std::uint32_t cl,
                       void *params, std::uint32_t params_size) const
{
    NVOS64_PARAMETERS p = {
        .hRoot         = this->root.handle,
        .hObjectParent = parent.handle,
        .hObjectNew    = obj.handle,
        .hClass        = cl,
        .pAllocParms   = params,
        .paramsSize    = params_size,
        .flags         = 0,
    };
    ENVID_CHECK_RM(nvesc_iowr(fd, NV_ESC_RM_ALLOC, &p), p.status);

    obj.handle = p.hObjectNew;
    obj.parent = parent.handle;

    return 0;
}

int Device::nvrm_free(int fd, Object &obj) const {
    NVOS00_PARAMETERS p = {
        .hRoot         = this->root.handle,
        .hObjectParent = obj.parent,
        .hObjectOld    = obj.handle,
    };
    ENVID_CHECK_RM(nvesc_iowr(fd, NV_ESC_RM_FREE, &p), p.status);

    return 0;
}

int Device::nvrm_control(int fd, const Object &obj, std::uint32_t cmd,
                         void *params, std::uint32_t params_size) const
{
    NVOS54_PARAMETERS p = {
        .hClient    = this->root.handle,
        .hObject    = obj.handle,
        .cmd        = cmd,
        .flags      = 0,
        .params     = params,
        .paramsSize = params_size,
    };
    ENVID_CHECK_RM(nvesc_iowr(fd, NV_ESC_RM_CONTROL, &p), p.status);

    return 0;
}

int Device::alloc_channel(int &idx, std::uint32_t engine_type) {
    idx = -1;

    for (std::size_t i = 0; i < this->channels_mask.size(); ++i) {
        auto &n = this->channels_mask[i];
        if (auto pos = std::countr_one(n); pos != Device::channel_mask_bitwidth) {
            n |= UINT64_C(1) << pos;
            idx = pos + i * Device::channel_mask_bitwidth + 1;
            return 0;
        }
    }

    return ENVIDEO_RC_SYSTEM(ENOMEM);
}

int Device::free_channel(int idx) {
    if (idx <= 0)
        return ENVIDEO_RC_SYSTEM(EINVAL);

    this->channels_mask[(idx - 1) / channel_mask_bitwidth] &= ~(UINT64_C(1) << ((idx - 1) & (channel_mask_bitwidth - 1)));

    return 0;
}

int Device::register_event(std::uint32_t notifier_type) {
    if (this->event_refs[notifier_type]++ != 0)
        return 0;

    ENVID_CHECK(this->nvrm_control(this->subdevice, NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION, NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS{
        .event  = notifier_type,
        .action = NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_SINGLE,
    }));

    return 0;
}

int Device::unregister_event(std::uint32_t notifier_type) {
    if (--this->event_refs[notifier_type] != 0)
        return 0;

    ENVID_CHECK(this->nvrm_control(this->subdevice, NV2080_CTRL_CMD_EVENT_SET_NOTIFICATION, NV2080_CTRL_EVENT_SET_NOTIFICATION_PARAMS{
        .event  = notifier_type,
        .action = NV2080_CTRL_EVENT_SET_NOTIFICATION_ACTION_DISABLE,
    }));

    return 0;
}

int Device::get_class_id(std::uint32_t engine_type, std::uint32_t &cl) const {
    NV2080_CTRL_GPU_GET_ENGINE_CLASSLIST_PARAMS eng_list = { .engineType = engine_type };
    ENVID_CHECK(this->nvrm_control(this->subdevice, NV2080_CTRL_CMD_GPU_GET_ENGINE_CLASSLIST, eng_list));
    if (!eng_list.numClasses)
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    std::vector<std::uint32_t> class_list(eng_list.numClasses);
    eng_list.classList = class_list.data();
    ENVID_CHECK(this->nvrm_control(this->subdevice, NV2080_CTRL_CMD_GPU_GET_ENGINE_CLASSLIST, eng_list));

    cl = class_list[0];
    return 0;
}

int Device::read_clocks(RUSD_CLK_PUBLIC_DOMAIN_INFOS &clk_info, bool update) const {
    if (update) {
        ENVID_CHECK(this->nvrm_control(this->rusd.object, NV00DE_CTRL_CMD_REQUEST_DATA_POLL, NV00DE_CTRL_REQUEST_DATA_POLL_PARAMS{
            .polledDataMask = NV00DE_RUSD_POLL_CLOCK,
        }));
    }

    RUSD_READ_DATA(static_cast<NV00DE_SHARED_DATA *>(this->rusd.cpu_addr), clkPublicDomainInfos, &clk_info);
    return 0;
}

void Device::kickoff(std::uint32_t token) const {
    auto mmio = reinterpret_cast<std::uintptr_t >(this->usermode.cpu_addr);
    volatile auto *doorbell = reinterpret_cast<std::uint32_t *>(mmio + NVC361_NOTIFY_CHANNEL_PENDING);

    *doorbell = token;
}

bool Device::probe() {
    int fd = ::open(Device::ctl_dev.data(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return false;

    ENVID_SCOPEGUARD([&fd] { ::close(fd); });

    std::array<nv_ioctl_card_info_t, 32> card_info = {};
    if (auto rc = nvesc_iow(fd, NV_ESC_CARD_INFO, &card_info); rc < 0)
        return false;

    return std::ranges::any_of(card_info, [](auto &i) { return i.valid; });
}

int Device::initialize() {
    // Open file interfaces
    std::ranges::copy(Device::ctl_dev, this->ctl_path.data());
    ENVID_CHECK_ERRNO(this->ctl_fd = ::open(this->ctl_path.data(), O_RDWR | O_CLOEXEC));

    // Find first available device minor number
    std::array<nv_ioctl_card_info_t, 32> card_info = {};
    ENVID_CHECK_ERRNO(nvesc_iow(this->ctl_fd, NV_ESC_CARD_INFO, &card_info));

    auto info = std::ranges::find_if(card_info, [](auto &i) { return i.valid; });
    if (info == card_info.end())
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    std::snprintf(this->card_path.data(), this->card_path.size(), Device::card_dev.data(), info->minor_number);
    ENVID_CHECK_ERRNO(this->card_fd = ::open(this->card_path.data(), O_RDWR | O_CLOEXEC));
    ENVID_CHECK(nvesc_iowr(this->card_fd, NV_ESC_REGISTER_FD, &this->ctl_fd));

    // Allocate base objects
    ENVID_CHECK(this->nvrm_alloc(Object{}, this->root, NV01_ROOT_CLIENT));

    NV0000_CTRL_GPU_GET_ID_INFO_V2_PARAMS gpu_info = { .gpuId = info->gpu_id };
    ENVID_CHECK(this->nvrm_control(this->root, NV0000_CTRL_CMD_GPU_GET_ID_INFO_V2, gpu_info));

    ENVID_CHECK(this->nvrm_alloc(this->root, this->device, NV01_DEVICE_0, NV0080_ALLOC_PARAMETERS{
        .deviceId     = gpu_info.deviceInstance,
        .hClientShare = this->root.handle,
    }));

    ENVID_CHECK(this->nvrm_alloc(this->device, this->subdevice, NV20_SUBDEVICE_0, NV2080_ALLOC_PARAMETERS{
        .subDeviceId = gpu_info.subDeviceInstance,
    }));

    // XXX: Is this correct?
    this->is_tegra = DRF_VAL(0000, _CTRL_GPU_ID_INFO, _SOC, gpu_info.gpuFlags);

    // Allocate and map user shared memory
    this->rusd.size = sizeof(NV00DE_SHARED_DATA);
    ENVID_CHECK(this->nvrm_alloc(this->subdevice, this->rusd.object, RM_USER_SHARED_DATA, NV00DE_ALLOC_PARAMETERS{
        .polledDataMask = NV00DE_RUSD_POLL_CLOCK,
    }));
    ENVID_CHECK(this->rusd.map_cpu(true));

    // Query which hardware classes are supported
    NV2080_CTRL_GPU_GET_ENGINES_V2_PARAMS engine_list = {};
    ENVID_CHECK(this->nvrm_control(this->subdevice, NV2080_CTRL_CMD_GPU_GET_ENGINES_V2, engine_list));

    this->engines.resize(engine_list.engineCount);
    std::ranges::copy(std::span(engine_list.engineList, engine_list.engineCount), this->engines.begin());

    NV0080_CTRL_GPU_GET_CLASSLIST_V2_PARAMS class_list = {};
    ENVID_CHECK(this->nvrm_control(this->device, NV0080_CTRL_CMD_GPU_GET_CLASSLIST_V2, class_list));

    this->classes.resize(class_list.numClasses);
    std::ranges::copy(std::span(class_list.classList, class_list.numClasses), this->classes.begin());

    auto usermode_cl = this->find_class(0x61), gpfifo_cl = this->find_class(0x6f);
    if (!usermode_cl || !gpfifo_cl)
        return ENVIDEO_RC_SYSTEM(ENOSYS);

    // Allocate address space
    ENVID_CHECK(this->nvrm_alloc(this->device, this->vaspace, NV01_MEMORY_VIRTUAL, NV_MEMORY_VIRTUAL_ALLOCATION_PARAMS{
        .offset = 0,
        .limit  = 0,
    }));

    // Allocate and map usermode mmio
    this->usermode.size = NVC361_NV_USERMODE__SIZE;
    ENVID_CHECK(this->nvrm_alloc(this->subdevice, this->usermode.object, usermode_cl));
    ENVID_CHECK(this->usermode.map_cpu());

    // Create OS event
    ENVID_CHECK_ERRNO(this->os_event_fd = ::open(this->card_path.data(), O_RDWR | O_CLOEXEC));
    ENVID_CHECK_ERRNO(nvesc_iowr(this->os_event_fd, NV_ESC_REGISTER_FD, &this->ctl_fd));

    nv_ioctl_alloc_os_event_t p = {
        .hClient = this->root.handle,
        .hDevice = this->device.handle,
        .fd      = static_cast<std::uint32_t>(this->os_event_fd),
    };
    ENVID_CHECK_RM(nvesc_iowr(this->os_event_fd, NV_ESC_ALLOC_OS_EVENT, &p), p.Status);

    // Allocate and map semaphore memory
    ENVID_CHECK(this->semaphores.initialize(0x1000, this->page_size));

    // Query capabilities
    std::uint32_t nvdec_cl;
    ENVID_CHECK(this->get_class_id(NV2080_ENGINE_TYPE_NVDEC(0), nvdec_cl));
    this->nvdec_version = get_nvdec_version(nvdec_cl);

    NV0080_CTRL_BSP_GET_CAPS_PARAMS_V2 nvdec_caps = { .instanceId = 0 };
    ENVID_CHECK(this->nvrm_control(this->device, NV0080_CTRL_CMD_BSP_GET_CAPS_V2, nvdec_caps));

    if (!(nvdec_caps.capsTbl[0] & util::bit(0))) {
        this->vp8_unsupported = this->vp9_unsupported = this->vp9_high_depth_unsupported = true;
    } else {
        this->vp8_unsupported =  !(nvdec_caps.capsTbl[4] & util::bit(2));
        this->vp9_unsupported = !!(nvdec_caps.capsTbl[3] & util::bit(1));

        if (!this->vp9_unsupported)
            this->vp9_high_depth_unsupported = !(nvdec_caps.capsTbl[4] & util::bit(4));
    }

    this->h264_unsupported = !!(nvdec_caps.capsTbl[2] & util::bit(0));
    this->hevc_unsupported = !!(nvdec_caps.capsTbl[1] & util::bit(0));

    this->av1_unsupported  = !!(nvdec_caps.capsTbl[3] & util::bit(0));

    return 0;
}

int Device::finalize() {
    this->nvrm_free(this->os_event);

    if (this->os_event_fd) {
        nv_ioctl_free_os_event_t p = {
            .hClient = this->root.handle,
            .hDevice = this->device.handle,
            .fd      = static_cast<std::uint32_t>(this->os_event_fd),
        };
        nvesc_iowr(this->os_event_fd, NV_ESC_FREE_OS_EVENT, &p);

        ::close(this->os_event_fd);
    }

    this->usermode.unmap_cpu();
    this->nvrm_free(this->ctl_fd, this->usermode.object);

    this->rusd.unmap_cpu();
    this->nvrm_free(this->ctl_fd, this->rusd.object);

    this->nvrm_free(this->ctl_fd, this->vaspace);
    this->nvrm_free(this->ctl_fd, this->subdevice);
    this->nvrm_free(this->ctl_fd, this->device);
    this->nvrm_free(this->ctl_fd, this->root);

    if (this->card_fd)
        ::close(this->card_fd);

    if (this->ctl_fd)
        ::close(this->ctl_fd);

    return 0;
}

bool Device::poll_internal(envid::Fence fence) const {
    // Wrapping comparison
    volatile auto *semas = static_cast<std::uint32_t *>(this->semaphores.cpu_addr);
    return static_cast<std::int32_t>(semas[fence_id(fence)] - fence_value(fence)) >= 0;
}

int Device::wait(envid::Fence fence, std::uint64_t timeout_us) {
    auto idx = (fence_id(fence) >> 1) + 1;
    if (!this->check_channel_idx(idx))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    // Convert to milliseconds
    std::int64_t timeout = timeout_us / 1000;

    struct pollfd p = {
        .fd     = this->os_event_fd,
        .events = POLLIN | POLLPRI,
    };

    auto start = std::chrono::steady_clock::now();
    while (true) {
        if (this->poll_internal(fence))
            break;

        auto rc = ::poll(&p, 1, 100);
        if (rc < 0)
            return ENVIDEO_RC_SYSTEM(errno);

        timeout -= std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        timeout  = std::max(timeout, INT64_C(0));

        if (!timeout)
            return ENVIDEO_RC_SYSTEM(ETIMEDOUT);
    }

    return 0;
}

int Device::poll(envid::Fence fence, bool &is_done) {
    auto idx = (fence_id(fence) >> 1) + 1;
    if (!this->check_channel_idx(idx))
        return ENVIDEO_RC_SYSTEM(EINVAL);

    is_done = this->poll_internal(fence);
    return 0;
}

int Map::map_cpu(bool system) {
    auto &d = *reinterpret_cast<Device *>(this->device);

    int map_fd;
    ENVID_CHECK_ERRNO(map_fd = ::open((system ? d.ctl_path : d.card_path).data(), O_RDWR | O_CLOEXEC));
    ENVID_SCOPEGUARD([&map_fd] { ::close(map_fd); });

    nv_ioctl_nvos33_parameters_with_fd p = {
        .params      = {
            .hClient = d.root.handle,
            .hDevice = d.device.handle,
            .hMemory = this->object.handle,
            .offset  = 0,
            .length  = this->size,
            .flags   = DRF_DEF(OS33, _FLAGS, _CACHING_TYPE, _DEFAULT) |
                       DRF_DEF(OS33, _FLAGS, _MAPPING,      _DIRECT),
        },
        .fd          = map_fd,
    };
    ENVID_CHECK_RM(nvesc_iowr(d.ctl_fd, NV_ESC_RM_MAP_MEMORY, &p), p.params.status);

    this->linear_address = NV_PTR_TO_NVUPTR(p.params.pLinearAddress);

    auto addr = ::mmap(nullptr, this->size, PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, 0);
    if (addr == MAP_FAILED)
        return ENVIDEO_RC_SYSTEM(errno);

    this->cpu_addr = addr;

    return 0;
}

int Map::unmap_cpu() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    if (this->own_mem && this->cpu_addr)
        ::munmap(this->cpu_addr, this->size);

    if (this->linear_address) {
        auto p = NVOS34_PARAMETERS{
            .hClient        = d.root.handle,
            .hDevice        = d.subdevice.handle,
            .hMemory        = this->object.handle,
            .pLinearAddress = NV_PTR_TO_NvP64(this->linear_address),
        };
        nvesc_iowr(d.ctl_fd, NV_ESC_RM_UNMAP_MEMORY, &p);
    }

    return 0;
}

int Map::map_gpu() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    NVOS46_PARAMETERS p = {
        .hClient = d.root.handle,
        .hDevice = d.device.handle,
        .hDma    = d.vaspace.handle,
        .hMemory = this->object.handle,
        .offset  = 0,
        .length  = this->size,
        .flags   = DRF_DEF(OS46, _FLAGS, _PAGE_SIZE, _DEFAULT),
    };
    ENVID_CHECK_RM(nvesc_iowr(d.ctl_fd, NV_ESC_RM_MAP_MEMORY_DMA, &p), p.status);

    this->gpu_addr_pitch = this->gpu_addr_block = p.dmaOffset;

    return 0;
}

int Map::unmap_gpu() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    if (this->gpu_addr_pitch) {
        auto p = NVOS34_PARAMETERS{
            .hClient        = d.root.handle,
            .hDevice        = d.subdevice.handle,
            .hMemory        = this->object.handle,
            .pLinearAddress = NV_PTR_TO_NvP64(this->gpu_addr_pitch),
        };
        ENVID_CHECK_RM(nvesc_iowr(d.ctl_fd, NV_ESC_RM_UNMAP_MEMORY, &p), p.status);
    }

    return 0;
}

int Map::initialize(std::size_t size, std::size_t align) {
    auto &d = *reinterpret_cast<Device *>(this->device);

    this->object.parent = d.device.handle;
    std::uint32_t attr, attr2, cl = get_memory_class(this->flags);
    ENVID_CHECK(d.nvrm_alloc(d.device, this->object, cl, NV_MEMORY_ALLOCATION_PARAMS{
        .owner     = d.root.handle,
        .type      = get_memory_type(this->flags),
        .flags     = get_alloc_flags(this->flags, &attr, &attr2),
        .attr      = attr,
        .attr2     = attr2,
        .size      = size,
        .alignment = align,
    }));

    this->size   = size;
    this->handle = this->object.handle;

    if (ENVIDEO_MAP_GET_CPU_FLAGS(this->flags) != EnvideoMap_CpuUnmapped)
        ENVID_CHECK(this->map_cpu(cl == NV01_MEMORY_SYSTEM));

    if (ENVIDEO_MAP_GET_GPU_FLAGS(this->flags) != EnvideoMap_GpuUnmapped)
        ENVID_CHECK(this->map_gpu());

    return 0;
}

int Map::initialize(void *address, std::size_t size, std::size_t align) {
    auto &d = *reinterpret_cast<Device *>(this->device);

    // This will undergo a kernel-side conversion step to NVOS32_DESCRIPTOR_TYPE_OS_PAGE_ARRAY,
    // so it cannot be created through NV_ESC_RM_ALLOC
    std::uint32_t attr, attr2;
    NVOS32_PARAMETERS p = {
        .hRoot                  = d.root.handle,
        .hObjectParent          = d.device.handle,
        .function               = NVOS32_FUNCTION_ALLOC_OS_DESCRIPTOR,
        .data = {
            .AllocOsDesc = {
                .type           = get_memory_type(this->flags),
                .flags          = get_alloc_flags(this->flags, &attr, &attr2, true),
                .attr           = attr,
                .attr2          = attr2,
                .descriptor     = NV_PTR_TO_NvP64(address),
                .limit          = size - 1,
                .descriptorType = NVOS32_DESCRIPTOR_TYPE_VIRTUAL_ADDRESS,
            },
        },
    };
    ENVID_CHECK_RM(nvesc_iowr(d.ctl_fd, NV_ESC_RM_VID_HEAP_CONTROL, &p), p.status);

    this->object  = { .handle = p.data.AllocOsDesc.hMemory, .parent = p.hObjectParent };
    this->size    = size;
    this->handle  = this->object.handle;
    this->own_mem = false;

    if (ENVIDEO_MAP_GET_CPU_FLAGS(this->flags) != EnvideoMap_CpuUnmapped)
        this->cpu_addr = address;

    if (ENVIDEO_MAP_GET_GPU_FLAGS(this->flags) != EnvideoMap_GpuUnmapped)
        ENVID_CHECK(this->map_gpu());

    return 0;
}

int Map::finalize() {
    auto &d = *reinterpret_cast<Device *>(this->device);

    this->unmap_gpu();
    this->unmap_cpu();
    d.nvrm_free(d.ctl_fd, this->object);

    return 0;
}

int Map::pin(envid::Channel *channel) {
    // Do nothing, all engines use the same address space
    return 0;
}

int Map::cache_op(std::size_t offset, std::size_t len,
                  EnvideoCacheFlags flags)
{
    auto &d = *reinterpret_cast<Device *>(this->device);

    // The kernel module will return an error in all situations, except invalidating cached host memory
    // Generic memory is always allocated in device memory
    if (ENVIDEO_MAP_GET_USAGE_FLAGS(this->flags) == EnvideoMap_UsageGeneric || flags != EnvideoCache_Invalidate)
        return 0;

    std::uint32_t op;
    switch (static_cast<int>(flags)) {
        case EnvideoCache_Writeback:
            op = NV0000_CTRL_OS_UNIX_FLAGS_USER_CACHE_FLUSH;
            break;
        case EnvideoCache_Invalidate:
            op = NV0000_CTRL_OS_UNIX_FLAGS_USER_CACHE_INVALIDATE;
            break;
        case EnvideoCache_Writeback | EnvideoCache_Invalidate:
            op = NV0000_CTRL_OS_UNIX_FLAGS_USER_CACHE_FLUSH_INVALIDATE;
            break;
        default:
            return -1;
    }

    ENVID_CHECK(d.nvrm_control(d.root, NV0000_CTRL_CMD_OS_UNIX_FLUSH_USER_CACHE, NV0000_CTRL_OS_UNIX_FLUSH_USER_CACHE_PARAMS{
        .offset   = offset,
        .length   = len,
        .cacheOps = op,
        .hDevice  = d.device.handle,
        .hObject  = this->object.handle,
    }));

    return 0;
}

} // namespace envid::nvidia
