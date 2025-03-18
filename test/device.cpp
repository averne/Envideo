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

TEST(DeviceTest, Basic) {
    EnvideoDevice *device;
    EXPECT_EQ(envideo_device_create (&device), 0);
    EXPECT_EQ(envideo_device_destroy( device), 0);

    EXPECT_NE(envideo_device_create (nullptr), 0);
    EXPECT_NE(envideo_device_destroy(nullptr), 0);
}

TEST(DeviceTest, Fence) {
    EnvideoDevice *device;
    EXPECT_EQ(envideo_device_create(&device), 0);

    bool is_done;
    EnvideoFence fence = {};
    EXPECT_NE(envideo_fence_poll(device, fence, &is_done), 0);
    EXPECT_NE(envideo_fence_poll(device, fence, nullptr),  0);

    EXPECT_NE(envideo_fence_wait(device, fence, UINT64_MAX), 0);
    EXPECT_NE(envideo_fence_wait(device, fence, 0),          0);

    EXPECT_EQ(envideo_device_destroy(device), 0);
}


struct ContraintsTest: public testing::TestWithParam<std::tuple<EnvideoCodec, EnvideoPixelFormat>> {
    ContraintsTest() { envideo_device_create(&this->dev); }
   ~ContraintsTest() { envideo_device_destroy(this->dev); }
    EnvideoDevice *dev = nullptr;
};

TEST_P(ContraintsTest, Basic) {
    EnvideoDecodeConstraints constraints = {
        .codec     = std::get<0>(GetParam()),
        .subsample = std::get<1>(GetParam()),
    };

    EXPECT_NE(envideo_get_decode_constraints(nullptr, &constraints), 0);
    EXPECT_NE(envideo_get_decode_constraints(dev,     nullptr),      0);

    for (int depth = 0; depth <= 16; ++depth) {
        constraints.depth = depth;

        EXPECT_EQ(envideo_get_decode_constraints(dev, &constraints), 0);

        switch (constraints.depth) {
            default:
                EXPECT_EQ(constraints.supported, false);
            case 8:
            case 10:
            case 12:
                break;
        }
    }
}

INSTANTIATE_TEST_CASE_P(ContraintsCombinations, ContraintsTest,
    ::testing::Combine(
        ::testing::ValuesIn({EnvideoCodec_Mjpeg, EnvideoCodec_Mpeg1, EnvideoCodec_Mpeg2, EnvideoCodec_Mpeg4, EnvideoCodec_Vc1,
                             EnvideoCodec_H264,  EnvideoCodec_H265,  EnvideoCodec_Vp8,   EnvideoCodec_Vp9,   EnvideoCodec_Av1}),
        ::testing::ValuesIn({EnvideoSubsampling_Monochrome, EnvideoSubsampling_420, EnvideoSubsampling_422,
                             EnvideoSubsampling_440,        EnvideoSubsampling_444})
    )
);
