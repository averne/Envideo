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

#ifndef ENVIDEO_H
#define ENVIDEO_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENVIDEO_BIT(x) (1 << (x))

#define ENVIDEO_RC_MOD_SYSTEM 0
#define ENVIDEO_RC_MOD_RM     1
#define ENVIDEO_RC_MOD_ENGINE 2

#define ENVIDEO_MKRC(res, mod) (-((res) | ((mod) << 28)))
#define ENVIDEO_RC_SYSTEM(res) ENVIDEO_MKRC(res, ENVIDEO_RC_MOD_SYSTEM)
#define ENVIDEO_RC_RM(res)     ENVIDEO_MKRC(res, ENVIDEO_RC_MOD_RM)
#define ENVIDEO_RC_ENGINE(res) ENVIDEO_MKRC(res, ENVIDEO_RC_MOD_ENGINE)

#define ENVIDEO_RC_MOD(rc) (((rc) >> 29) & 3)
#define ENVIDEO_RC_RES(rc) ((rc) & ((1 << 29) - 1))

/*
 * The low 8 bits of buffer addresses are ignored in the multimedia engine command stream,
 * so all allocations to must be aligned consequently.
 */
#define ENVIDEO_MAP_ALIGN (1 << 8)

/*
 * GOBs are 64B wide.
 */
#define ENVIDEO_WIDTH_ALIGN(bpp) (64 / (bpp))

/*
 * GOBs are 8B high, and we use a GOB height of 2.
 * We double this requirement to make sure it is respected for the subsampled chroma plane.
 */
#define ENVIDEO_HEIGHT_ALIGN(bpp) (32)

typedef enum {
    EnvideoCodec_Mjpeg,
    EnvideoCodec_Mpeg1,
    EnvideoCodec_Mpeg2,
    EnvideoCodec_Mpeg4,
    EnvideoCodec_Vc1,
    EnvideoCodec_H264,
    EnvideoCodec_H265,
    EnvideoCodec_Vp8,
    EnvideoCodec_Vp9,
    EnvideoCodec_Av1,
} EnvideoCodec;

typedef enum {
    EnvideoSubsampling_Monochrome,
    EnvideoSubsampling_420,
    EnvideoSubsampling_422,
    EnvideoSubsampling_440,
    EnvideoSubsampling_444,
} EnvideoPixelFormat;

typedef enum {
    EnvideoPlatform_Linux      = 0,
    EnvideoPlatform_Hos        = 1,
    EnvideoPlatform_Windows    = 2,

    EnvideoPlatform_Nvidia     = 0 << 8,
    EnvideoPlatform_Nvgpu      = 1 << 8,
    EnvideoPlatform_Nouveau    = 2 << 8,

    EnvideoPlatform_Invalid    = UINT32_C(-1),

    EnvideoPlatform_OsMask     = 0x00ff,
    EnvideoPlatform_DriverMask = 0xff00,
} EnvideoPlatform;

#define ENVIDEO_PLATFORM_GET_OS(p)     ((EnvideoPlatform)((p) & EnvideoPlatform_OsMask))
#define ENVIDEO_PLATFORM_GET_DRIVER(p) ((EnvideoPlatform)((p) & EnvideoPlatform_DriverMask))

typedef enum {
    EnvideoMap_CpuCacheable     = 0 << 0,
    EnvideoMap_CpuWriteCombine  = 1 << 0,
    EnvideoMap_CpuUncacheable   = 2 << 0,
    EnvideoMap_CpuUnmapped      = 3 << 0,

    EnvideoMap_GpuCacheable     = 0 << 4,
    EnvideoMap_GpuUncacheable   = 1 << 4,
    EnvideoMap_GpuUnmapped      = 2 << 4,

    EnvideoMap_UsageGeneric     = 0 << 8,
    EnvideoMap_UsageFramebuffer = 1 << 8,
    EnvideoMap_UsageEngine      = 2 << 8,
    EnvideoMap_UsageCmdbuf      = 3 << 8,

    EnvideoMap_CpuMask          = 0x000f,
    EnvideoMap_GpuMask          = 0x00f0,
    EnvideoMap_UsageMask        = 0x0f00,
} EnvideoMapFlags;

#define ENVIDEO_MAP_GET_CPU_FLAGS(f)   ((EnvideoMapFlags)((f) & EnvideoMap_CpuMask))
#define ENVIDEO_MAP_GET_GPU_FLAGS(f)   ((EnvideoMapFlags)((f) & EnvideoMap_GpuMask))
#define ENVIDEO_MAP_GET_USAGE_FLAGS(f) ((EnvideoMapFlags)((f) & EnvideoMap_UsageMask))

typedef enum {
    EnvideoCache_Writeback  = ENVIDEO_BIT(0),
    EnvideoCache_Invalidate = ENVIDEO_BIT(1),
} EnvideoCacheFlags;

typedef enum {
    EnvideoEngine_Host,
    EnvideoEngine_Copy,
    EnvideoEngine_Nvdec,
    EnvideoEngine_Nvenc,
    EnvideoEngine_Nvjpg,
    EnvideoEngine_Ofa,
    EnvideoEngine_Vic,
} EnvideoEngine;

typedef enum {
    EnvideoRelocType_Default,
    EnvideoRelocType_Pitch,
    EnvideoRelocType_Tiled,
} EnvideoRelocType;

typedef struct EnvideoDevice  EnvideoDevice;
typedef struct EnvideoMap     EnvideoMap;
typedef struct EnvideoChannel EnvideoChannel;
typedef struct EnvideoCmdbuf  EnvideoCmdbuf;
typedef uint64_t              EnvideoFence;

typedef struct {
    bool     is_tegra;
    uint64_t reserved[3];
} EnvideoDeviceInfo;

int envideo_device_create(EnvideoDevice **device);
int envideo_device_destroy(EnvideoDevice *device);
EnvideoDeviceInfo envideo_device_get_info(EnvideoDevice *device);

int envideo_fence_wait(EnvideoDevice *device, EnvideoFence fence, uint64_t timeout_us);
int envideo_fence_poll(EnvideoDevice *device, EnvideoFence fence, bool *is_done);

int envideo_map_create(EnvideoDevice *device, EnvideoMap **map, size_t size, size_t align, EnvideoMapFlags flags);
int envideo_map_from_va(EnvideoDevice *device, EnvideoMap **map, void *mem, size_t size, size_t align, EnvideoMapFlags flags);
int envideo_map_destroy(EnvideoMap *map);
int envideo_map_realloc(EnvideoMap *map, size_t size, size_t align);
int envideo_map_pin(EnvideoMap *map, EnvideoChannel *channel);
int envideo_map_cache_op(EnvideoMap *map, size_t offset, size_t len, EnvideoCacheFlags flags);
size_t   envideo_map_get_size    (EnvideoMap *map);
uint32_t envideo_map_get_handle  (EnvideoMap *map);
void    *envideo_map_get_cpu_addr(EnvideoMap *map);
uint64_t envideo_map_get_gpu_addr(EnvideoMap *map);

int envideo_channel_create(EnvideoDevice *device, EnvideoChannel **channel, EnvideoEngine engine);
int envideo_channel_destroy(EnvideoChannel *channel);
int envideo_channel_submit(EnvideoChannel *channel, EnvideoCmdbuf *cmdbuf, EnvideoFence *fence);

int envideo_cmdbuf_create(EnvideoChannel *channel, EnvideoCmdbuf **cmdbuf);
int envideo_cmdbuf_destroy(EnvideoCmdbuf *cmdbuf);
int envideo_cmdbuf_add_memory(EnvideoCmdbuf *cmdbuf, const EnvideoMap *map, uint32_t offset, uint32_t size);
int envideo_cmdbuf_clear(EnvideoCmdbuf *cmdbuf);
int envideo_cmdbuf_begin(EnvideoCmdbuf *cmdbuf, EnvideoEngine engine);
int envideo_cmdbuf_end(EnvideoCmdbuf *cmdbuf);
int envideo_cmdbuf_push_word(EnvideoCmdbuf *cmdbuf, uint32_t word);
int envideo_cmdbuf_push_value(EnvideoCmdbuf *cmdbuf, uint32_t offset, uint32_t value);
int envideo_cmdbuf_push_reloc(EnvideoCmdbuf *cmdbuf, uint32_t offset, const EnvideoMap *target, uint32_t target_offset,
                              EnvideoRelocType reloc_type, int shift);
int envideo_cmdbuf_wait_fence(EnvideoCmdbuf *cmdbuf, EnvideoFence fence);
int envideo_cmdbuf_cache_op(EnvideoCmdbuf *cmdbuf, EnvideoCacheFlags flags);

int envideo_dfs_initialize(EnvideoChannel *channel, float framerate);
int envideo_dfs_finalize(EnvideoChannel *channel);
int envideo_dfs_set_damping(EnvideoChannel *channel, double damping);
int envideo_dfs_update(EnvideoChannel *channel, int len, int cycles);
int envideo_dfs_commit(EnvideoChannel *channel);

typedef struct {
    EnvideoMap *map;
    uint32_t    map_offset;
    uint32_t    width;
    uint32_t    height;
    uint32_t    stride;
    bool        tiled;
    uint8_t     gob_height;
} EnvideoSurfaceInfo;

int envideo_surface_transfer(EnvideoCmdbuf *cmdbuf, EnvideoSurfaceInfo *src, EnvideoSurfaceInfo *dst);

typedef struct {
    EnvideoCodec codec;
    EnvideoPixelFormat subsample;
    int depth;

    bool supported;
    uint32_t min_width, min_height;
    uint32_t max_width, max_height;
    uint32_t max_mbs;
} EnvideoDecodeConstraints;

int envideo_get_decode_constraints(EnvideoDevice *device, EnvideoDecodeConstraints *constraints);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // ENVIDEO_H
