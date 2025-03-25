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
#include <nvmisc.h>
#include <clc76f.h>

#include "common.hpp"

struct ChannelTest: public testing::Test {
    ChannelTest() { envideo_device_create(&this->dev); }
   ~ChannelTest() { envideo_device_destroy(this->dev); }
    EnvideoDevice *dev = nullptr;
};

TEST_F(ChannelTest, Basic) {
    EnvideoChannel *channel;

    auto engine = EnvideoEngine_Copy;

    EXPECT_EQ(envideo_channel_create(dev, &channel, engine), 0);
    EXPECT_EQ(envideo_channel_destroy(channel), 0);

    EXPECT_NE(envideo_channel_create(nullptr, &channel, engine), 0);
    EXPECT_NE(envideo_channel_create(dev, nullptr,      engine), 0);
}

struct EngineTest: public testing::TestWithParam<EnvideoEngine> {
    EngineTest() { envideo_device_create(&this->dev); }
   ~EngineTest() { envideo_device_destroy(this->dev); }
    EnvideoDevice *dev = nullptr;
};

TEST_P(EngineTest, Basic) {
    EnvideoChannel *channel;

    auto engine = GetParam();
    switch (engine) {
        case EnvideoEngine_Host:
            EXPECT_NE(envideo_channel_create(dev, &channel, engine), 0);
            break;
        case EnvideoEngine_Copy:
        case EnvideoEngine_Nvdec:
        case EnvideoEngine_Nvenc:
            // These engines should always be present
            EXPECT_EQ(envideo_channel_create(dev, &channel, engine), 0);
            EXPECT_EQ(envideo_channel_destroy(channel), 0);
            break;
        case EnvideoEngine_Nvjpg:
        case EnvideoEngine_Ofa:
        case EnvideoEngine_Vic:
            // These engines can be missing
            if (envideo_channel_create(dev, &channel, engine))
                GTEST_SKIP();
            else
                EXPECT_EQ(envideo_channel_destroy(channel), 0);
            break;
        default:
            FAIL();
    }
}

INSTANTIATE_TEST_CASE_P(AllEngine, EngineTest,
    ::testing::ValuesIn({EnvideoEngine_Host,
                         EnvideoEngine_Copy,  EnvideoEngine_Nvdec, EnvideoEngine_Nvenc,
                         EnvideoEngine_Nvjpg, EnvideoEngine_Ofa,   EnvideoEngine_Vic})
);

struct JobTest: public testing::Test {
    JobTest() {
        envideo_device_create(&this->dev);
        envideo_channel_create(this->dev, &this->chan, EnvideoEngine_Copy);
        envideo_map_create(this->dev, &this->cmdbuf_map, 0x10000, 0x1000,
            static_cast<EnvideoMapFlags>(EnvideoMap_CpuWriteCombine | EnvideoMap_GpuUncacheable |
                                         EnvideoMap_LocationHost    | EnvideoMap_UsageCmdbuf));
        envideo_map_pin(this->cmdbuf_map, this->chan);
        envideo_cmdbuf_create(this->chan, &this->cmdbuf);
        envideo_cmdbuf_add_memory(this->cmdbuf, this->cmdbuf_map, 0, envideo_map_get_size(this->cmdbuf_map));
    }

    ~JobTest() {
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

TEST_F(JobTest, Basic) {
    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Host), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC76F_NOP, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoFence fence;
    EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
    EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);

    EXPECT_NE(envideo_channel_submit(nullptr, cmdbuf,  &fence),  0);
    EXPECT_NE(envideo_channel_submit(chan,    nullptr, &fence),  0);
    EXPECT_NE(envideo_channel_submit(chan,    cmdbuf,  nullptr), 0);
}

TEST_F(JobTest, Event) {
    EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Host), 0);
    EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC76F_NOP, 0), 0);
    EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

    EnvideoChannel *chan2;
    EXPECT_EQ(envideo_channel_create (this->dev, &chan2, EnvideoEngine_Copy), 0);
    EXPECT_EQ(envideo_channel_destroy(chan2),                                 0);

    EnvideoFence fence;
    EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
    EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);
}

TEST_F(JobTest, Wrap) {
    for (std::uint64_t i = 0; i < 0x1000; ++i) {
        EXPECT_EQ(envideo_cmdbuf_clear(cmdbuf), 0);
        EXPECT_EQ(envideo_cmdbuf_begin(cmdbuf, EnvideoEngine_Host), 0);
        EXPECT_EQ(envideo_cmdbuf_push_value(cmdbuf, NVC76F_NOP, 0), 0);
        EXPECT_EQ(envideo_cmdbuf_end(cmdbuf), 0);

        EnvideoFence fence;
        EXPECT_EQ(envideo_channel_submit(chan, cmdbuf, &fence), 0);
        EXPECT_EQ(envideo_fence_wait(dev, fence, 5e6), 0);
    }
}
