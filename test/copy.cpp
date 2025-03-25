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
#include <cstdlib>
#include <cstring>
#include <tuple>

#include <xxhash.h>
#include <gtest/gtest.h>

#include <envideo.h>
#include <nvmisc.h>
#include <clc7b5.h>

#include "common.hpp"

struct CopyTest: public testing::Test {
    CopyTest() {
        envideo_device_create(&this->dev);
        envideo_channel_create(this->dev, &this->chan, EnvideoEngine_Copy);
        envideo_map_create(this->dev, &this->cmdbuf_map, 0x10000, 0x1000,
            static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuUncacheable |
                                         EnvideoMap_LocationHost    | EnvideoMap_UsageCmdbuf));
        envideo_map_pin(this->cmdbuf_map, this->chan);
        envideo_cmdbuf_create(this->chan, &this->cmdbuf);
        envideo_cmdbuf_add_memory(this->cmdbuf, this->cmdbuf_map, 0, envideo_map_get_size(this->cmdbuf_map));
    }

    ~CopyTest() {
        envideo_cmdbuf_destroy (this->cmdbuf);
        envideo_map_destroy    (this->cmdbuf_map);
        envideo_channel_destroy(this->chan);
        envideo_device_destroy (this->dev);
    }

    EnvideoDevice  *dev        = nullptr;
    EnvideoChannel *chan       = nullptr;
    EnvideoMap     *cmdbuf_map = nullptr;
    EnvideoCmdbuf  *cmdbuf     = nullptr;
};

TEST_F(CopyTest, Memset) {
    EnvideoMap *map;

    auto size = 0x100000, align = 0x1000;

    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable |
                                              EnvideoMap_LocationHost | EnvideoMap_UsageFramebuffer);
    EXPECT_EQ(envideo_map_create(dev, &map, size, align, flags), 0);
    EXPECT_EQ(envideo_map_pin(map, chan), 0);

    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Copy), 0);
    EXPECT_EQ(envideo_cmdbuf_push_reloc(cmdbuf, NVC7B5_OFFSET_OUT_UPPER, map, 0, EnvideoRelocType_Pitch, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LINE_LENGTH_IN,    size), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_SET_REMAP_CONST_A, 0xcc), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_SET_REMAP_COMPONENTS,
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _DST_X,              _CONST_A) |
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _COMPONENT_SIZE,     _ONE)     |
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _NUM_DST_COMPONENTS, _ONE)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LAUNCH_DMA,
        DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NON_PIPELINED) |
        DRF_DEF(C7B5, _LAUNCH_DMA, _FLUSH_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _MULTI_LINE_ENABLE,  _FALSE)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _REMAP_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_TYPE,           _VIRTUAL)       |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_TYPE,           _VIRTUAL)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_cache_op(cmdbuf, EnvideoCache_Writeback), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoFence fence;
    EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, envideo_map_get_size(map), EnvideoCache_Invalidate), 0);
    EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);

    // In : xxhash.xxh64_hexdigest(b"\xcc" * 0x100000)
    // Out: 'be85ef1c71f4bbbe'
    EXPECT_EQ(XXH64(envideo_map_get_cpu_addr(map), envideo_map_get_size(map), 0), 0xbe85ef1c71f4bbbe);

    EXPECT_EQ(envideo_map_destroy(map), 0);
}

TEST_F(CopyTest, MemsetFromVa) {
    EnvideoMap *map;

    auto size = 0x100000, align = 0x1000;
    auto memset_off = 0x100, memset_size = size - memset_off - 0x200;

    auto *mem = new (std::align_val_t(align)) std::uint8_t[size];
    std::memset(mem, 0xaa, memset_off);
    std::memset(mem + memset_off + memset_size, 0xbb, size - memset_size - memset_off);

    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable |
                                              EnvideoMap_LocationHost | EnvideoMap_UsageFramebuffer);
    EXPECT_EQ(envideo_map_from_va(dev, &map, mem, size, align, flags), 0);

    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Copy), 0);
    EXPECT_EQ(envideo_cmdbuf_push_reloc(cmdbuf, NVC7B5_OFFSET_OUT_UPPER, map, memset_off, EnvideoRelocType_Pitch, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LINE_LENGTH_IN,    memset_size), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_SET_REMAP_CONST_A, 0xcc),        0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_SET_REMAP_COMPONENTS,
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _DST_X,              _CONST_A) |
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _COMPONENT_SIZE,     _ONE)     |
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _NUM_DST_COMPONENTS, _ONE)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LAUNCH_DMA,
        DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NON_PIPELINED) |
        DRF_DEF(C7B5, _LAUNCH_DMA, _FLUSH_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _MULTI_LINE_ENABLE,  _FALSE)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _REMAP_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_TYPE,           _VIRTUAL)       |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_TYPE,           _VIRTUAL)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_cache_op(cmdbuf, EnvideoCache_Writeback), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoFence fence;
    EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, envideo_map_get_size(map), EnvideoCache_Invalidate), 0);
    EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);

    // In : xxhash.xxh64_hexdigest(b"\xaa" * 0x100 + b"\xcc" * 0xffd00 + b"\xbb" * 0x200)
    // Out: '0da2d6cadfbe565f'
    EXPECT_EQ(XXH64(envideo_map_get_cpu_addr(map), envideo_map_get_size(map), 0), 0x0da2d6cadfbe565f);

    EXPECT_EQ(envideo_map_destroy(map), 0);
}

TEST_F(CopyTest, Memcpy) {
    EnvideoMap *src, *dst;

    auto size = 0x100000, align = 0x1000;

    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable |
                                              EnvideoMap_LocationHost | EnvideoMap_UsageFramebuffer);
    EXPECT_EQ(envideo_map_create(dev, &src, size, align, flags), 0);
    EXPECT_EQ(envideo_map_create(dev, &dst, size, align, flags), 0);
    EXPECT_EQ(envideo_map_pin(src, chan), 0);
    EXPECT_EQ(envideo_map_pin(dst, chan), 0);

    std::memset(envideo_map_get_cpu_addr(src), 0x11, envideo_map_get_size(src));
    EXPECT_EQ(envideo_map_cache_op(src, 0, envideo_map_get_size(src), EnvideoCache_Writeback), 0);

    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Copy), 0);
    EXPECT_EQ(envideo_cmdbuf_push_reloc(cmdbuf, NVC7B5_OFFSET_IN_UPPER,  src, 0, EnvideoRelocType_Default, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_push_reloc(cmdbuf, NVC7B5_OFFSET_OUT_UPPER, dst, 0, EnvideoRelocType_Default, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LINE_LENGTH_IN, size), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LAUNCH_DMA,
        DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NON_PIPELINED) |
        DRF_DEF(C7B5, _LAUNCH_DMA, _FLUSH_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _MULTI_LINE_ENABLE,  _FALSE)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _REMAP_ENABLE,       _FALSE)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_TYPE,           _VIRTUAL)       |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_TYPE,           _VIRTUAL)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_cache_op(cmdbuf, EnvideoCache_Writeback), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoFence fence;
    EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
    EXPECT_EQ(envideo_map_cache_op(dst, 0, envideo_map_get_size(dst), EnvideoCache_Invalidate), 0);
    EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);

    // In : xxhash.xxh64_hexdigest(b"\xaa" * 0x100000)
    // Out: '8b16293e51d6e10c'
    EXPECT_EQ(XXH64(envideo_map_get_cpu_addr(dst), envideo_map_get_size(dst), 0), 0x8b16293e51d6e10c);

    EXPECT_EQ(envideo_map_destroy(dst), 0);
    EXPECT_EQ(envideo_map_destroy(src), 0);
}

TEST_F(CopyTest, Image) {
    EnvideoMap *src, *dst;

    std::uint32_t width = 1920, height = 1080,
        size = width * height, align = 0x1000;
    EnvideoMapFlags flags;

    flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuUnmapped    | EnvideoMap_GpuCacheable |
                                         EnvideoMap_LocationDevice | EnvideoMap_UsageFramebuffer);
    EXPECT_EQ(envideo_map_create(dev, &src, size, align, flags), 0);
    EXPECT_EQ(envideo_map_pin(src, chan), 0);

    flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable |
                                         EnvideoMap_LocationHost | EnvideoMap_UsageFramebuffer);
    EXPECT_EQ(envideo_map_create(dev, &dst, size, align, flags), 0);
    EXPECT_EQ(envideo_map_pin(dst, chan), 0);

    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Copy), 0);
    EXPECT_EQ(envideo_cmdbuf_push_reloc(cmdbuf, NVC7B5_OFFSET_OUT_UPPER, src, 0, EnvideoRelocType_Pitch, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_PITCH_IN,          width),  0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_PITCH_OUT,         width),  0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LINE_LENGTH_IN,    width),  0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LINE_COUNT,        height), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_SET_REMAP_CONST_A, 0xaa),   0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_SET_REMAP_COMPONENTS,
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _DST_X,              _CONST_A) |
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _COMPONENT_SIZE,     _ONE)     |
        DRF_DEF(C7B5, _SET_REMAP_COMPONENTS, _NUM_DST_COMPONENTS, _ONE)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC7B5_LAUNCH_DMA,
        DRF_DEF(C7B5, _LAUNCH_DMA, _DATA_TRANSFER_TYPE, _NON_PIPELINED) |
        DRF_DEF(C7B5, _LAUNCH_DMA, _FLUSH_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _SRC_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_MEMORY_LAYOUT,  _PITCH)         |
        DRF_DEF(C7B5, _LAUNCH_DMA, _MULTI_LINE_ENABLE,  _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _REMAP_ENABLE,       _TRUE)          |
        DRF_DEF(C7B5, _LAUNCH_DMA, _DST_TYPE,           _VIRTUAL)
    ), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoSurfaceInfo src_info = {
        .map        = src,
        .map_offset = 0,
        .width      = width,
        .height     = height,
        .stride     = width,
    }, dst_info = {
        .map        = dst,
        .map_offset = 0,
        .width      = width,
        .height     = height,
        .stride     = width,
    };
    EXPECT_EQ(envideo_surface_transfer(cmdbuf, &src_info, &dst_info), 0);

    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Host), 0);
    EXPECT_EQ(envideo_cmdbuf_cache_op(cmdbuf, EnvideoCache_Writeback), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoFence fence;
    EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
    EXPECT_EQ(envideo_map_cache_op(dst, 0, envideo_map_get_size(dst), EnvideoCache_Invalidate), 0);
    EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);

    // In : xxhash.xxh64_hexdigest(b"\xa" * 1920 * 1080)
    // Out: '538a2a80c0e10548'
    EXPECT_EQ(XXH64(envideo_map_get_cpu_addr(dst), size, 0), 0x538a2a80c0e10548);

    EXPECT_EQ(envideo_map_destroy(dst), 0);
    EXPECT_EQ(envideo_map_destroy(src), 0);
}
