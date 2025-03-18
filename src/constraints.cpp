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

#include "common.hpp"

namespace envid {

NvdecVersion get_nvdec_version(int cl) {
    switch ((cl >> 8) & 0xff) {
        case 0xa0: return NvdecVersion::V10; // Kepler
        case 0xb0: return NvdecVersion::V11; // Maxwell A
        case 0xb6: return NvdecVersion::V20; // Maxwell B
        case 0xc1: return NvdecVersion::V30; // Pascal A
        case 0xc2: return NvdecVersion::V31; // Pascal B
        case 0xc3: return NvdecVersion::V32; // Volta
        case 0xc4: return NvdecVersion::V40; // Turing
        case 0xc6: return NvdecVersion::V41; // Ampere A
        case 0xb8: return NvdecVersion::V42; // Hopper
        case 0xc7: return NvdecVersion::V50; // Ampere B
        case 0xc9: return NvdecVersion::V51; // Ada
        case 0xcd: return NvdecVersion::V60; // Blackwell A
        case 0xcf: return NvdecVersion::V61; // Blackwell B
        default:   return NvdecVersion::None;
    }
}

int get_decode_constraints(EnvideoDevice *device, EnvideoDecodeConstraints *constraints) {
    constraints->supported = false;

    if ((constraints->depth != 8) && (constraints->depth != 10) && (constraints->depth != 12))
        return 0;

    // Verify that the relevant engine is available
    switch (constraints->codec) {
        case EnvideoCodec_Mpeg1 ... EnvideoCodec_Av1:
            if (device->nvdec_version == NvdecVersion::None)
                return 0;
            break;
        case EnvideoCodec_Mjpeg:
            if (device->nvjpg_version == NvjpgVersion::None)
                return 0;
            break;
        default:
            return ENVIDEO_RC_SYSTEM(EINVAL);
    }

    auto set_dec_constraints = [constraints](std::uint32_t min_width, std::uint32_t min_height,
                                             std::uint32_t max_width, std::uint32_t max_height,
                                             std::uint32_t max_mbs)
    {
        constraints->supported  = true;
        constraints->min_width  = min_width;
        constraints->min_height = min_height;
        constraints->max_width  = max_width;
        constraints->max_height = max_height;
        constraints->max_mbs    = max_mbs;
    };

    // Values taken from the nvcuvid library, except for MJPEG
    switch (constraints->codec) {
        case EnvideoCodec_Mjpeg:
            if (constraints->depth != 8)
                return 0;

            if (device->nvjpg_version > NvjpgVersion::V13)
                return 0;

            // Values for NVJPG1.0 (taken from the nvtvmr/nvmedia/nvmmlite_video libraries on L4T)
            set_dec_constraints(0x10, 0x10, 0x4000, 0x4000, UINT32_C(-1));

            break;

        case EnvideoCodec_Mpeg1:
        case EnvideoCodec_Mpeg2:
            if (constraints->depth != 8 || constraints->subsample != EnvideoSubsampling_420)
                return 0;

            set_dec_constraints(0x30, 0x10, 0xff0, 0xff0, 0xff00);

            break;

        case EnvideoCodec_Mpeg4:
        case EnvideoCodec_Vc1:
            if (constraints->depth != 8 || constraints->subsample != EnvideoSubsampling_420)
                return 0;

            set_dec_constraints(0x30, 0x10, 0x7f0, 0x7f0, 0x2000);

            break;

        case EnvideoCodec_H264:
            if (device->h264_unsupported)
                return 0;

            if (device->nvdec_version >= NvdecVersion::V60) {
                if (constraints->depth > 10)
                    return 0;

                if (constraints->subsample != EnvideoSubsampling_420 && constraints->subsample != EnvideoSubsampling_422)
                    return 0;

                set_dec_constraints(0x30, 0x40, 0x2000, 0x2000, 0x40000);
            } else {
                if (constraints->depth > 8 || constraints->subsample != EnvideoSubsampling_420)
                    return 0;

                set_dec_constraints(0x30, 0x10, 0x1000, 0x1000, 0x10000);
            }

            break;

        case EnvideoCodec_H265:
            if (device->hevc_unsupported)
                return 0;

            if (device->nvdec_version >= NvdecVersion::V60) {
                if (constraints->subsample != EnvideoSubsampling_420 && constraints->subsample != EnvideoSubsampling_422 &&
                    constraints->subsample != EnvideoSubsampling_444)
                    return 0;

                set_dec_constraints(0x90, 0x90, 0x2000, 0x2000, 0x40000);
            } else if (device->nvdec_version >= NvdecVersion::V40) {
                if (constraints->subsample != EnvideoSubsampling_420 && constraints->subsample != EnvideoSubsampling_444)
                    return 0;

                set_dec_constraints(0x90, 0x90, 0x2000, 0x2000, 0x40000);
            } else if (device->nvdec_version >= NvdecVersion::V31) {
                if (constraints->subsample != EnvideoSubsampling_420)
                    return 0;

                set_dec_constraints(0x90, 0x90, 0x2000, 0x2000, 0x40000);
            } else if (device->nvdec_version <= NvdecVersion::V30) {
                if (constraints->subsample != EnvideoSubsampling_420)
                    return 0;

                set_dec_constraints(0x90, 0x90, 0x1000, 0x1000, 0x10000);
            } else if (device->nvdec_version >= NvdecVersion::V20) {
                if (constraints->subsample != EnvideoSubsampling_420 || constraints->depth > 10)
                    return 0;

                set_dec_constraints(0x90, 0x90, 0x1000, 0x1000, 0x9000);
            } else {
                return 0;
            }

            break;

        case EnvideoCodec_Vp8:
            if (device->vp8_unsupported || device->nvdec_version < NvdecVersion::V20)
                return 0;

            if (constraints->depth != 8 || constraints->subsample != EnvideoSubsampling_420)
                return 0;

            set_dec_constraints(0x30, 0x10, 0x1000, 0x1000, 0x10000);

            break;

        case EnvideoCodec_Vp9:
            if (device->vp9_unsupported || constraints->subsample != EnvideoSubsampling_420)
                return 0;

            if (device->nvdec_version >= NvdecVersion::V31) {
                if (constraints->depth > 8 && device->vp9_high_depth_unsupported)
                    return 0;

                set_dec_constraints(0x80, 0x80, 0x2000, 0x2000, 0x40000);
            } else if (device->nvdec_version >= NvdecVersion::V30) {
                if (constraints->depth > 8)
                    return 0;

                set_dec_constraints(0x80, 0x80, 0x1000, 0x1000, 0x10000);
            } else if (device->nvdec_version >= NvdecVersion::V20) {
                if (constraints->depth > 8)
                    return 0;

                set_dec_constraints(0x80, 0x80, 0x1000, 0x1000, 0x9000);
            } else {
                return 0;
            }

            break;

        case EnvideoCodec_Av1:
            if (device->av1_unsupported || constraints->depth > 10)
                return 0;

            if (constraints->subsample != EnvideoSubsampling_Monochrome && constraints->subsample != EnvideoSubsampling_420)
                return 0;

            if (device->nvdec_version < NvdecVersion::V50)
                return 0;

            set_dec_constraints(0x80, 0x80, 0x2000, 0x2000, 0x40000);

            break;

        default:
            return ENVIDEO_RC_SYSTEM(EINVAL);
    }

    return 0;
}

} // namespace envid
