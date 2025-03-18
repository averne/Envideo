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

#include <vector>

#include "util.hpp"
#include "common.hpp"

#include <nvhost_ioctl.h>

namespace envid {

class GpfifoCmdbuf final: public Cmdbuf {
    public:
        constexpr static auto num_entries = 0x800;

    public:
        GpfifoCmdbuf(bool use_syncpts): use_syncpts(use_syncpts) {}
        virtual int initialize()                                          override;
        virtual int finalize()                                            override;
        virtual int clear()                                               override;
        virtual int begin(EnvideoEngine engine)                           override;
        virtual int end()                                                 override;
        virtual int push_word(std::uint32_t word)                         override;
        virtual int push_value(std::uint32_t offset, std::uint32_t value) override;
        virtual int push_reloc(std::uint32_t offset, const envid::Map *target, std::uint32_t target_offset,
                               EnvideoRelocType reloc_type, int shift)    override;
        virtual int wait_fence(envid::Fence fence)                        override;
        virtual int cache_op(EnvideoCacheFlags flags)                     override;

    public:
        std::vector<std::uint64_t> entries;

    private:
        bool use_syncpts;
        std::uint32_t cur_subchannel = 0;
        int num_words = 0;
};

class Host1xCmdbuf final: public Cmdbuf {
    private:
        constexpr static auto initial_cap_cmdbufs = 3;
        constexpr static auto initial_cap_relocs  = 15;
        constexpr static auto initial_cap_syncpts = 3;

    public:
        Host1xCmdbuf(bool need_setclass): need_setclass(need_setclass) {}
        virtual int initialize()                                          override;
        virtual int finalize()                                            override;
        virtual int clear()                                               override;
        virtual int begin(EnvideoEngine engine)                           override;
        virtual int end()                                                 override;
        virtual int push_word(std::uint32_t word)                         override;
        virtual int push_value(std::uint32_t offset, std::uint32_t value) override;
        virtual int push_reloc(std::uint32_t offset, const envid::Map *target, std::uint32_t target_offset,
                               EnvideoRelocType reloc_type, int shift)    override;
        virtual int wait_fence(envid::Fence fence)                        override;
        virtual int cache_op(EnvideoCacheFlags flags)                     override;

        int add_syncpt_incr(std::uint32_t syncpt);

    public:
        std::vector<nvhost_cmdbuf>      cmdbufs;
        std::vector<nvhost_cmdbuf_ext>  cmdbuf_exts;
        std::vector<std::uint32_t>      class_ids;

        std::vector<nvhost_reloc>       relocs;
        std::vector<nvhost_reloc_type>  reloc_types;
        std::vector<nvhost_reloc_shift> reloc_shifts;

        std::vector<nvhost_syncpt_incr> syncpt_incrs;
        std::vector<std::uint32_t>      fences;

    private:
        bool need_setclass;
};

} // namespace envid
