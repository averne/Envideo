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

struct CmdbufTest: public testing::Test {
    CmdbufTest() {
        envideo_device_create(&this->dev);
        envideo_channel_create(this->dev, &this->chan, EnvideoEngine_Copy);
        envideo_map_create(this->dev, &this->cmdbuf_map, 0x10000, 0x1000,
            static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuCacheable |
                                         EnvideoMap_LocationHost    | EnvideoMap_UsageCmdbuf));
        envideo_map_pin(this->cmdbuf_map, this->chan);
    }

    ~CmdbufTest() {
        envideo_map_destroy    (this->cmdbuf_map);
        envideo_channel_destroy(this->chan);
        envideo_device_destroy (this->dev);
    }

    EnvideoDevice  *dev        = nullptr;
    EnvideoChannel *chan       = nullptr;
    EnvideoMap     *cmdbuf_map = nullptr;
};

TEST_F(CmdbufTest, Basic) {
    EnvideoCmdbuf *cmdbuf;

    auto size = envideo_map_get_size(cmdbuf_map);

    EXPECT_EQ(envideo_cmdbuf_create    (chan, &cmdbuf),        0);
    EXPECT_EQ(envideo_cmdbuf_add_memory(cmdbuf, cmdbuf_map, 0, size), 0);
    EXPECT_EQ(envideo_cmdbuf_clear     (cmdbuf),               0);
    EXPECT_EQ(envideo_cmdbuf_destroy   (cmdbuf),               0);

    EXPECT_NE(envideo_cmdbuf_create (nullptr, &cmdbuf), 0);
    EXPECT_NE(envideo_cmdbuf_create (chan,    nullptr), 0);
    EXPECT_NE(envideo_cmdbuf_destroy(nullptr),          0);

    EXPECT_EQ(envideo_cmdbuf_create(chan, &cmdbuf), 0);

    EXPECT_NE(envideo_cmdbuf_add_memory(cmdbuf,  cmdbuf_map, 0,    size + 1),                    0);
    EXPECT_NE(envideo_cmdbuf_add_memory(nullptr, cmdbuf_map, 0,    size),                        0);
    EXPECT_NE(envideo_cmdbuf_add_memory(cmdbuf,  nullptr,    0,    size),                        0);
    EXPECT_NE(envideo_cmdbuf_clear     (nullptr),                                                0);
    EXPECT_NE(envideo_cmdbuf_begin     (nullptr, EnvideoEngine_Host),                            0);
    EXPECT_NE(envideo_cmdbuf_end       (nullptr),                                                0);
    EXPECT_NE(envideo_cmdbuf_push_word (nullptr, 0),                                             0);
    EXPECT_NE(envideo_cmdbuf_push_value(nullptr, 0, 0),                                          0);
    EXPECT_NE(envideo_cmdbuf_push_reloc(nullptr, 0, cmdbuf_map, 0, EnvideoRelocType_Default, 0), 0);
    EXPECT_NE(envideo_cmdbuf_push_reloc(cmdbuf,  0, nullptr,    0, EnvideoRelocType_Default, 0), 0);
    EXPECT_NE(envideo_cmdbuf_wait_fence(nullptr, 0),                                             0);
    EXPECT_NE(envideo_cmdbuf_cache_op  (nullptr, EnvideoCache_Writeback),                        0);

    EXPECT_EQ(envideo_cmdbuf_destroy(cmdbuf), 0);
}

TEST_F(CmdbufTest, Limit) {
    EnvideoCmdbuf *cmdbuf;

    auto size = envideo_map_get_size(cmdbuf_map) - 1;

    EXPECT_EQ(envideo_cmdbuf_create    (chan, &cmdbuf),               0);
    EXPECT_EQ(envideo_cmdbuf_add_memory(cmdbuf, cmdbuf_map, 0, size), 0);

    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Host), 0);

    // We created a copy channel, which hosted by the gpfifo engine on all platforms
    // We should be able to write precisely the number of dwords that we reserved for the command buffer,
    // because it should not sneak in extraneous SetClass/other commands that would disrupt our count
    // This would not work on eg. a host1x channel on HOS
    for (std::size_t i = 0; i < size / sizeof(std::uint32_t); ++i)
        EXPECT_EQ(envideo_cmdbuf_push_word(cmdbuf, 0), 0);

    // The command buffer should be full
    EXPECT_NE(envideo_cmdbuf_push_word(cmdbuf, 0), 0);

    EXPECT_EQ(envideo_cmdbuf_end    (cmdbuf), 0);
    EXPECT_EQ(envideo_cmdbuf_destroy(cmdbuf), 0);
}
