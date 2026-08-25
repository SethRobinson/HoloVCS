// CANONICAL COPY NOTE: this file is kept in sync with the Azahar fork at
// f:/Unreal/azahar/src/citra_libretro/holo/holo_layer_abi.h - edit both together.
// HoloVCS layer-delivery ABI ("v2", successor of the beetle-vb HoloVB.h scheme).
// Plain C structs shared between the patched emulator core (this repo) and the HoloVCS
// frontend (which keeps its own copy at cores/holo_abi/holo_layer_abi.h - keep in sync,
// bump HOLO_LAYER_ABI_VERSION on any layout change).
//
// Contract: once per emulated frame the core calls the callback registered through the
// exported retro_set_video_refresh_holo() with
//   data/width/height/pitch: a composite frame of the top screen (BGRA bytes, row-major,
//                            landscape) the frontend may blit with its normal path, and
//   info: the depth-sliced layers. Slice pixels are BGRA bytes; a pixel whose 4 bytes are
//         all zero is "empty" (transparent). Slice 0 is the NEAREST layer. Buffers are
//         owned by the core and valid only during the callback.

#ifndef HOLO_LAYER_ABI_H
#define HOLO_LAYER_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOLO_LAYER_ABI_VERSION 3

typedef struct HoloLayerSlice {
    int32_t used;        /* nonzero when this slice has any content this frame */
    float depth01;       /* representative scene depth of the band, 0 = nearest .. 1 = farthest */
    int32_t width;       /* pixel dimensions of this slice's image */
    int32_t height;
    int32_t pitchBytes;  /* bytes per row */
    uint8_t* pixels;     /* BGRA, width*height, all-zero pixel = transparent */
} HoloLayerSlice;

typedef struct HoloLayerInfo {
    uint32_t abiVersion; /* HOLO_LAYER_ABI_VERSION */
    int32_t layerCount;  /* number of entries in layers[]; 0 = nearest */
    int32_t layersUsed;  /* how many have used != 0 this frame */
    HoloLayerSlice* layers;
    HoloLayerSlice bottom; /* v3: the composed 3DS BOTTOM screen (320x240, opaque, flat) */
} HoloLayerInfo;

typedef void (*retro_video_refresh_holo_t)(const void* data, unsigned width, unsigned height,
                                           size_t pitch, const struct HoloLayerInfo* info);

#ifdef __cplusplus
}
#endif

#endif /* HOLO_LAYER_ABI_H */
