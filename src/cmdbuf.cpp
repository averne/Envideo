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

#include <errno.h>

#include <nvmisc.h>

#include <clc76f.h>
#include <host1x.h>

#include "cmdbuf.hpp"

namespace envid {

namespace {

constexpr inline std::uint32_t engine_to_subchannel(EnvideoEngine engine) {
    switch (engine) {
        case EnvideoEngine_Copy:
        case EnvideoEngine_Nvdec:
        case EnvideoEngine_Nvenc:
        case EnvideoEngine_Ofa:
            return 4;
        case EnvideoEngine_Host:
            return 6;
        default:
            return UINT32_C(-1);
    }
}

constexpr inline std::uint32_t reloc_type_to_host1x(EnvideoRelocType type) {
    switch (type) {
        case EnvideoRelocType_Default:
            return NVHOST_RELOC_TYPE_DEFAULT;
        case EnvideoRelocType_Pitch:
            return NVHOST_RELOC_TYPE_PITCH_LINEAR;
        case EnvideoRelocType_Tiled:
            return NVHOST_RELOC_TYPE_BLOCK_LINEAR;
        default:
            return UINT32_C(-1);
    }
}

} // namespace

int Cmdbuf::add_memory(const envid::Map *map, std::uint32_t offset, std::uint32_t size) {
    if (offset + size > map->size)
        return ENVIDEO_RC_SYSTEM(EINVAL);

    this->map        = map;
    this->mem_offset = offset;
    this->mem_size   = size;
    return this->clear();
}

int GpfifoCmdbuf::initialize() {
    return 0;
}

int GpfifoCmdbuf::finalize() {
    return 0;
}

int GpfifoCmdbuf::clear() {
    this->cur_word  = this->words();

    this->entries.clear();
    return 0;
}

int GpfifoCmdbuf::begin(EnvideoEngine engine) {
    this->cur_num_words  = 0;
    this->cur_engine     = engine;
    this->cur_subchannel = engine_to_subchannel(engine);

    auto mem_offset = reinterpret_cast<uintptr_t>(this->cur_word) - reinterpret_cast<std::uintptr_t>(this->map->cpu_addr);
    auto gpu_addr = this->map->gpu_addr_pitch + mem_offset;

    auto entry0 = DRF_NUM(C76F, _GP_ENTRY0, _GET,    gpu_addr >> 2);
    auto entry1 = DRF_NUM(C76F, _GP_ENTRY1, _GET_HI, gpu_addr >> 32);
    this->entries.emplace_back(entry0 | (entry1 << 32));

    return 0;
}

int GpfifoCmdbuf::end() {
    auto &entry = this->entries.back();
    entry |= DRF_NUM64(C76F, _GP_ENTRY1, _LENGTH, this->cur_num_words) << 32;
    return 0;
}

int GpfifoCmdbuf::push_word(std::uint32_t word) {
    if ((this->num_words() + 1) * sizeof(std::uint32_t) >= this->mem_size)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    *this->cur_word++ = word;
    ++this->cur_num_words;

    return 0;
}

int GpfifoCmdbuf::push_value(std::uint32_t offset, std::uint32_t value) {
    auto word = DRF_DEF(C76F, _DMA_INCR, _OPCODE,     _VALUE)               |
                DRF_NUM(C76F, _DMA_INCR, _SUBCHANNEL, this->cur_subchannel) |
                DRF_NUM(C76F, _DMA_INCR, _ADDRESS,    offset >> 2)          |
                DRF_NUM(C76F, _DMA_INCR, _COUNT,      1);

    ENVID_CHECK(this->push_word(word));
    ENVID_CHECK(this->push_word(value));
    return 0;
}

int GpfifoCmdbuf::push_reloc(std::uint32_t offset, const envid::Map *target, std::uint32_t target_offset,
                             EnvideoRelocType reloc_type, int shift)
{
    auto gpu_addr    = (reloc_type != EnvideoRelocType_Tiled) ? target->gpu_addr_pitch : target->gpu_addr_block;
    auto target_addr = (gpu_addr + target_offset) >> shift;

    // The GPU has 40 bits of address space, thus if the shift is larger or equal to 8,
    // the address will fit in a single register push
    if (shift >= 8) {
        ENVID_CHECK(this->push_value(offset, target_addr));
    } else {
        auto word = DRF_DEF(C76F, _DMA_INCR, _OPCODE,     _VALUE)               |
                    DRF_NUM(C76F, _DMA_INCR, _SUBCHANNEL, this->cur_subchannel) |
                    DRF_NUM(C76F, _DMA_INCR, _ADDRESS,    offset >> 2)          |
                    DRF_NUM(C76F, _DMA_INCR, _COUNT,      2);

        ENVID_CHECK(this->push_word(word));
        ENVID_CHECK(this->push_word(static_cast<std::uint32_t>(target_addr >> 32)));
        ENVID_CHECK(this->push_word(static_cast<std::uint32_t>(target_addr >> 0)));
    }

    return 0;
}

int GpfifoCmdbuf::wait_fence(envid::Fence fence) {
    if (this->use_syncpts && !this->syncpt_va_base) {
        auto word1 = DRF_DEF(C76F, _DMA_INCR, _OPCODE,     _VALUE)                                   |
                     DRF_NUM(C76F, _DMA_INCR, _SUBCHANNEL, engine_to_subchannel(EnvideoEngine_Host)) |
                     DRF_NUM(C76F, _DMA_INCR, _ADDRESS,    NVC76F_SYNCPOINTA >> 2)                   |
                     DRF_NUM(C76F, _DMA_INCR, _COUNT,      2);
        auto word2 = DRF_DEF(C76F, _SYNCPOINTB, _OPERATION,    _WAIT) |
                     DRF_DEF(C76F, _SYNCPOINTB, _WAIT_SWITCH,  _EN)   |
                     DRF_NUM(C76F, _SYNCPOINTB, _SYNCPT_INDEX, fence_id(fence));

        ENVID_CHECK(this->push_word(word1));
        ENVID_CHECK(this->push_word(fence_value(fence)));
        ENVID_CHECK(this->push_word(word2));
    } else {
        std::uint64_t gpu_addr;
        if (!this->use_syncpts) {
            // Assume that the map being written to belongs to the same device as the fence
            auto *map = this->map->device->get_semaphore_map();
            if (!map)
                return ENVIDEO_RC_SYSTEM(ENOMEM);

            gpu_addr = map->gpu_addr_pitch + fence_id(fence) * sizeof(std::uint32_t);
        } else {
            gpu_addr = this->syncpt_va_base + fence_id(fence) * this->syncpt_page_size;
        }

        auto word = DRF_DEF(C76F, _SEM_EXECUTE, _OPERATION,          _ACQ_CIRC_GEQ) |
                    DRF_DEF(C76F, _SEM_EXECUTE, _ACQUIRE_SWITCH_TSG, _EN);

        // Unlike other engines, this takes addresses in litte-endian format, so we can't use the push_reloc helper (epic)
        this->push_value(NVC76F_SEM_ADDR_LO, gpu_addr >> 0);
        this->push_value(NVC76F_SEM_ADDR_HI, gpu_addr >> 32);
        this->push_value(NVC76F_SEM_PAYLOAD_LO, fence_value(fence));
        this->push_value(NVC76F_SEM_EXECUTE, word);
    }

    return 0;
}

int GpfifoCmdbuf::cache_op(EnvideoCacheFlags flags) {
    // Multimedia engines are not connected to the L2 cache
    if (engine_is_multimedia(this->cur_engine))
        return 0;

    auto word = 0;
    if (flags & EnvideoCache_Writeback)
        word |= DRF_DEF(C76F, _MEM_OP_D, _OPERATION, _L2_FLUSH_DIRTY);
    if (flags & EnvideoCache_Invalidate)
        word |= DRF_DEF(C76F, _MEM_OP_D, _OPERATION, _L2_SYSMEM_INVALIDATE);

    // Host wait-for-idle
    ENVID_CHECK(this->push_value(NVC76F_SET_REFERENCE, 0));

    // Writes to MEM_OP_D must be preceded by MEM_OP_A/C (see dev_pbdma.ref.txt)
    ENVID_CHECK(this->push_value(NVC76F_MEM_OP_A, 0));
    ENVID_CHECK(this->push_value(NVC76F_MEM_OP_B, 0));
    ENVID_CHECK(this->push_value(NVC76F_MEM_OP_C, 0));
    ENVID_CHECK(this->push_value(NVC76F_MEM_OP_D, word));
    return 0;
}

int Host1xCmdbuf::initialize() {
#ifndef CONFIG_TEGRA_DRM
    this->cmdbufs     .reserve(Host1xCmdbuf::initial_cap_cmdbufs);
    this->cmdbuf_exts .reserve(Host1xCmdbuf::initial_cap_cmdbufs);
    this->class_ids   .reserve(Host1xCmdbuf::initial_cap_cmdbufs);
    this->relocs      .reserve(Host1xCmdbuf::initial_cap_relocs);
    this->reloc_types .reserve(Host1xCmdbuf::initial_cap_relocs);
    this->reloc_shifts.reserve(Host1xCmdbuf::initial_cap_relocs);
    this->syncpt_incrs.reserve(Host1xCmdbuf::initial_cap_syncpts);
    this->fences      .reserve(Host1xCmdbuf::initial_cap_syncpts);
#else
    this->cmds.reserve(Host1xCmdbuf::initial_cap_cmdbufs + Host1xCmdbuf::initial_cap_syncpts);
    this->bufs.reserve(Host1xCmdbuf::initial_cap_relocs);
#endif
    return 0;
}

int Host1xCmdbuf::finalize() {
    return 0;
}

int Host1xCmdbuf::clear() {
#ifndef CONFIG_TEGRA_DRM
    this->cmdbufs     .clear(); this->cmdbuf_exts.clear(); this->class_ids   .clear();
    this->relocs      .clear(); this->reloc_types.clear(); this->reloc_shifts.clear();
    this->syncpt_incrs.clear(); this->fences     .clear();
#else
    this->cmds.clear();
    this->bufs.clear();
#endif

    this->cur_word = this->words();
    return 0;
}

int Host1xCmdbuf::begin(EnvideoEngine engine) {
    this->cur_engine = engine;

    auto class_id = engine_to_host1x_class_id(engine);

#ifndef CONFIG_TEGRA_DRM
    this->cmdbufs    .emplace_back(this->map->handle, this->num_words() * sizeof(std::uint32_t));
    this->cmdbuf_exts.emplace_back(-1);
    this->class_ids  .emplace_back(class_id);
#else
    this->cmds.emplace_back(DRM_TEGRA_SUBMIT_CMD_GATHER_UPTR);
#endif

    if (this->need_setclass) {
        auto word = DRF_DEF(HOST, _HCFSETCL, _OPCODE,  _VALUE)   |
                    DRF_NUM(HOST, _HCFSETCL, _CLASSID, class_id) |
                    DRF_NUM(HOST, _HCFSETCL, _MASK,    0)        |
                    DRF_NUM(HOST, _HCFSETCL, _OFFSET,  0);
        this->push_word(word);
    }

    return 0;
}

int Host1xCmdbuf::end() {
    return 0;
}

int Host1xCmdbuf::push_word(std::uint32_t word) {
    if ((this->num_words() + 1) * sizeof(std::uint32_t) >= this->mem_size)
        return ENVIDEO_RC_SYSTEM(ENOMEM);

    *this->cur_word++ = word;

#ifndef CONFIG_TEGRA_DRM
    auto &cmdbuf = this->cmdbufs.back();
    ++cmdbuf.words;
#else
    auto &cmd = this->cmds.back();
    ++cmd.gather_uptr.words;
#endif

    return 0;
}

int Host1xCmdbuf::push_value(std::uint32_t offset, std::uint32_t value) {
    auto word = DRF_DEF(HOST, _HCFINCR, _OPCODE, _VALUE)              |
                DRF_NUM(HOST, _HCFINCR, _OFFSET, NV_THI_METHOD0 >> 2) |
                DRF_NUM(HOST, _HCFINCR, _COUNT,  2);

    ENVID_CHECK(this->push_word(word));
    ENVID_CHECK(this->push_word(offset >> 2));
    ENVID_CHECK(this->push_word(value));
    return 0;
}

int Host1xCmdbuf::push_reloc(std::uint32_t offset, const envid::Map *target, std::uint32_t target_offset,
                             EnvideoRelocType reloc_type, int shift)
{
#ifndef CONFIG_TEGRA_DRM
    if (auto iova = target->find_pin(this->cur_engine); iova != 0) {
        ENVID_CHECK(this->push_value(offset, (iova + target_offset) >> shift));
    } else if (auto type = reloc_type_to_host1x(reloc_type); type != UINT32_MAX) {
        ENVID_CHECK(this->push_value(offset, 0xdeadbeef));

        this->relocs.emplace_back(this->map->handle,
            (this->num_words() - 1) * sizeof(std::uint32_t), target->handle, target_offset);

        this->reloc_types .emplace_back(type);
        this->reloc_shifts.emplace_back(shift);
    } else {
        return ENVIDEO_RC_SYSTEM(EINVAL);
    }
#else
    if (auto id = target->find_pin(this->cur_engine); id != 0) {
        ENVID_CHECK(this->push_value(offset, 0xdeadbeef));

        this->bufs.emplace_back(drm_tegra_submit_buf{
            .mapping                 = static_cast<std::uint32_t>(id),
            .reloc = {
                .target_offset       = target_offset,
                .gather_offset_words = static_cast<std::uint32_t>(this->num_words() - 1),
                .shift               = static_cast<std::uint32_t>(shift),
            },
        });
    } else {
        return ENVIDEO_RC_SYSTEM(EINVAL);
    }
#endif

    return 0;
}

int Host1xCmdbuf::wait_fence(envid::Fence fence) {
#ifndef CONFIG_TEGRA_DRM
    auto mask = (1 << ((NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD - NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD) >> 2)) |
                (1 << ((NV_CLASS_HOST_WAIT_SYNCPT         - NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD) >> 2));
    auto word = DRF_DEF(HOST, _HCFMASK, _OPCODE, _VALUE)                                 |
                DRF_NUM(HOST, _HCFMASK, _OFFSET, NV_CLASS_HOST_LOAD_SYNCPT_PAYLOAD >> 2) |
                DRF_NUM(HOST, _HCFMASK, _MASK,   mask);

    ENVID_CHECK(this->push_word(word));
    ENVID_CHECK(this->push_word(fence_value(fence)));
    ENVID_CHECK(this->push_word(fence_id   (fence)));
#else
    this->cmds.emplace_back(drm_tegra_submit_cmd{
        .type        = DRM_TEGRA_SUBMIT_CMD_WAIT_SYNCPT,
        .wait_syncpt = drm_tegra_submit_cmd_wait_syncpt{ fence_id(fence), fence_value(fence) },
    });
#endif

    return 0;
}

int Host1xCmdbuf::cache_op(EnvideoCacheFlags flags) {
    // Multimedia engines are not connected to the L2 cache
    return 0;
}

int Host1xCmdbuf::add_syncpt_incr(std::uint32_t syncpt) {
#ifndef CONFIG_TEGRA_DRM
    this->syncpt_incrs.emplace_back(syncpt, 1);
    this->fences      .emplace_back(0);
#endif

    auto word1 = DRF_DEF(HOST, _HCFNONINCR, _OPCODE, _VALUE) |
                 DRF_NUM(HOST, _HCFNONINCR, _OFFSET, NV_THI_INCR_SYNCPT >> 2) |
                 DRF_NUM(HOST, _HCFNONINCR, _COUNT,  1);

    auto word2 = this->host1x_version < 6 ?
                 DRF_NUM(_THI, _INCR_SYNCPT, _INDX,  syncpt) |
                 DRF_DEF(_THI, _INCR_SYNCPT, _COND,  _OP_DONE) :
                 DRF_NUM(_THI, _INCR_SYNCPT, _INDX6, syncpt) |
                 DRF_DEF(_THI, _INCR_SYNCPT, _COND6, _OP_DONE);

    ENVID_CHECK(this->push_word(word1));
    ENVID_CHECK(this->push_word(word2));
    return 0;
}

} // namespace envid
