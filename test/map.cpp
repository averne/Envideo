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

#include <gtest/gtest.h>

#include <envideo.h>

#include "common.hpp"

struct MapTest: public testing::Test {
    MapTest() { envideo_device_create(&this->dev); }
   ~MapTest() { envideo_device_destroy(this->dev); }
    EnvideoDevice *dev = nullptr;
};

TEST_F(MapTest, Basic) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable);

    EXPECT_EQ(envideo_map_create(dev, &map, size, align, flags), 0);
    EXPECT_NE(envideo_map_get_handle  (map), 0);
    EXPECT_NE(envideo_map_get_cpu_addr(map), nullptr);
    EXPECT_NE(envideo_map_get_gpu_addr(map), 0);
    EXPECT_GE(envideo_map_get_size    (map), size);
    EXPECT_EQ(envideo_map_destroy(map), 0);

    EXPECT_EQ(envideo_map_get_handle  (nullptr), 0);
    EXPECT_GE(envideo_map_get_size    (nullptr), 0);
    EXPECT_EQ(envideo_map_get_cpu_addr(nullptr), nullptr);
    EXPECT_EQ(envideo_map_get_gpu_addr(nullptr), 0);

    EXPECT_NE(envideo_map_create(nullptr, &map, size, align, flags), 0);
    EXPECT_NE(envideo_map_create(dev, nullptr,  size, align, flags), 0);

    EXPECT_NE(envideo_map_create(dev, &map, 0, align, flags), 0);
    EXPECT_NE(envideo_map_create(dev, &map, size, 0,  flags), 0);
}

TEST_F(MapTest, FromVa) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuCacheable);

    auto *mem = new (std::align_val_t(align)) std::uint8_t[size];

    EXPECT_EQ(envideo_map_from_va(dev, &map, mem, size, align, flags), 0);
    EXPECT_NE(envideo_map_get_handle  (map), 0);
    EXPECT_NE(envideo_map_get_cpu_addr(map), nullptr);
    EXPECT_NE(envideo_map_get_gpu_addr(map), 0);
    EXPECT_GE(envideo_map_get_size    (map), size);
    EXPECT_EQ(envideo_map_destroy(map), 0);

    EXPECT_NE(envideo_map_from_va(nullptr, &map, mem, size, align, flags), 0);
    EXPECT_NE(envideo_map_from_va(dev, nullptr,  mem, size, align, flags), 0);
    EXPECT_NE(envideo_map_from_va(nullptr, &map, mem, size, align, flags), 0);
    EXPECT_NE(envideo_map_from_va(dev, &map, nullptr, size, align, flags), 0);

    delete[] mem;
}

TEST_F(MapTest, Realloc) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable);

    EXPECT_EQ(envideo_map_create(dev, &map, size, align, flags), 0);

    auto new_size = 0x10000;
    EXPECT_EQ(envideo_map_realloc(map, new_size, 0x1000), 0);
    EXPECT_NE(envideo_map_get_handle  (map), 0);
    EXPECT_NE(envideo_map_get_cpu_addr(map), nullptr);
    EXPECT_NE(envideo_map_get_gpu_addr(map), 0);
    EXPECT_GE(envideo_map_get_size    (map), new_size);
    EXPECT_EQ(envideo_map_destroy(map), 0);
}

TEST_F(MapTest, Cache) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable);

    EXPECT_EQ(envideo_map_create(dev, &map, size, align, flags), 0);

    EXPECT_NE(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(0                                               )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Writeback                          )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Invalidate                         )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Writeback | EnvideoCache_Invalidate)), 0);

    EXPECT_EQ(envideo_map_destroy(map), 0);
}

TEST_F(MapTest, Pin) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(EnvideoMap_CpuCacheable | EnvideoMap_GpuCacheable);

    EnvideoChannel *channel;
    EXPECT_EQ(envideo_channel_create(dev, &channel, EnvideoEngine_Copy), 0);
    EXPECT_EQ(envideo_map_create(dev, &map, size, align, flags), 0);

    EXPECT_EQ(envideo_map_pin(map,     channel), 0);
    EXPECT_EQ(envideo_map_pin(map,     channel), 0);
    EXPECT_NE(envideo_map_pin(nullptr, channel), 0);
    EXPECT_NE(envideo_map_pin(map,     nullptr), 0);

    EXPECT_EQ(envideo_map_destroy(map), 0);
    EXPECT_EQ(envideo_channel_destroy(channel), 0);
}

struct FlagTest: public testing::TestWithParam<std::tuple<EnvideoMapFlags, EnvideoMapFlags, EnvideoMapFlags, EnvideoMapFlags>> {
    FlagTest() { envideo_device_create(&this->dev); }
   ~FlagTest() { envideo_device_destroy(this->dev); }
    EnvideoDevice *dev = nullptr;
};

TEST_P(FlagTest, Basic) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(std::get<0>(GetParam()) | std::get<1>(GetParam()) |
                                              std::get<2>(GetParam()) | std::get<3>(GetParam()));

    EXPECT_EQ(envideo_map_create(dev, &map, size, align, flags), 0);

    EXPECT_NE(envideo_map_get_handle  (map), 0);
    EXPECT_GE(envideo_map_get_size    (map), size);

    if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) == EnvideoMap_CpuUnmapped)
        EXPECT_EQ(envideo_map_get_cpu_addr(map), nullptr);
    else
        EXPECT_NE(envideo_map_get_cpu_addr(map), nullptr);

    if (ENVIDEO_MAP_GET_GPU_FLAGS(flags) == EnvideoMap_GpuUnmapped)
        EXPECT_EQ(envideo_map_get_gpu_addr(map), 0);
    else
        EXPECT_NE(envideo_map_get_gpu_addr(map), 0);

    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Writeback                          )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Invalidate                         )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Writeback | EnvideoCache_Invalidate)), 0);

    EXPECT_EQ(envideo_map_destroy(map), 0);
}

TEST_P(FlagTest, FromVa) {
    EnvideoMap *map;

    auto size = 0x1000, align = 0x1000;
    auto flags = static_cast<EnvideoMapFlags>(std::get<0>(GetParam()) | std::get<1>(GetParam()) | std::get<2>(GetParam()));

    auto *mem = new (std::align_val_t(align)) std::uint8_t[size];

    EXPECT_EQ(envideo_map_from_va(dev, &map, mem, size, align, flags), 0);

    EXPECT_NE(envideo_map_get_handle  (map), 0);
    EXPECT_GE(envideo_map_get_size    (map), size);

    if (ENVIDEO_MAP_GET_CPU_FLAGS(flags) == EnvideoMap_CpuUnmapped)
        EXPECT_EQ(envideo_map_get_cpu_addr(map), nullptr);
    else
        EXPECT_NE(envideo_map_get_cpu_addr(map), nullptr);

    if (ENVIDEO_MAP_GET_GPU_FLAGS(flags) == EnvideoMap_GpuUnmapped)
        EXPECT_EQ(envideo_map_get_gpu_addr(map), 0);
    else
        EXPECT_NE(envideo_map_get_gpu_addr(map), 0);

    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Writeback                          )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Invalidate                         )), 0);
    EXPECT_EQ(envideo_map_cache_op(map, 0, size, static_cast<EnvideoCacheFlags>(EnvideoCache_Writeback | EnvideoCache_Invalidate)), 0);

    EXPECT_EQ(envideo_map_destroy(map), 0);

}

INSTANTIATE_TEST_CASE_P(FlagCombinations, FlagTest,
    ::testing::Combine(
        ::testing::ValuesIn({EnvideoMap_CpuCacheable, EnvideoMap_CpuWriteCombine,  EnvideoMap_CpuUncacheable, EnvideoMap_CpuUnmapped}),
        ::testing::ValuesIn({EnvideoMap_GpuCacheable, EnvideoMap_GpuUncacheable,   EnvideoMap_GpuUnmapped                           }),
        ::testing::ValuesIn({EnvideoMap_LocationHost, EnvideoMap_LocationDevice                                                     }),
        ::testing::ValuesIn({EnvideoMap_UsageGeneric, EnvideoMap_UsageFramebuffer, EnvideoMap_UsageEngine,    EnvideoMap_UsageCmdbuf})
    )
);
