/* Copyright (c) V-Nova International Limited 2023-2025. All rights reserved.
 * This software is licensed under the BSD-3-Clause-Clear License by V-Nova Limited.
 * No patent licenses are granted under this license. For enquiries about patent licenses,
 * please contact legal@v-nova.com.
 * The LCEVCdec software is a stand-alone project and is NOT A CONTRIBUTION to any other project.
 * If the software is incorporated into another project, THE TERMS OF THE BSD-3-CLAUSE-CLEAR LICENSE
 * AND THE ADDITIONAL LICENSING INFORMATION CONTAINED IN THIS FILE MUST BE MAINTAINED, AND THE
 * SOFTWARE DOES NOT AND MUST NOT ADOPT THE LICENSE OF THE INCORPORATING PROJECT. However, the
 * software may be incorporated into a project under a compatible license provided the requirements
 * of the BSD-3-Clause-Clear license are respected, and V-Nova Limited remains
 * licensor of the software ONLY UNDER the BSD-3-Clause-Clear license (not the compatible license).
 * ANY ONWARD DISTRIBUTION, WHETHER STAND-ALONE OR AS PART OF ANY OTHER PROJECT, REMAINS SUBJECT TO
 * THE EXCLUSION OF PATENT LICENSES PROVISION OF THE BSD-3-CLAUSE-CLEAR LICENSE. */

#include "apply_cmdbuffer_common.h"

#include <LCEVC/build_config.h>

#if VN_CORE_FEATURE(NEON)

#include "apply_cmdbuffer_common.h"
#include "fp_types.h"

#include <assert.h>
#include <LCEVC/common/limit.h>
#include <LCEVC/common/neon.h>

/*------------------------------------------------------------------------------*/

static inline int16x4_t loadPixelsDD(const int16_t* src)
{
    int16x4_t res = vld1_dup_s16(src);
    res = vld1_lane_s16(src + 1, res, 1);
    return res;
}

static inline uint8x8_t loadPixelsDD_U8(const uint8_t* src)
{
    uint8x8_t res = vld1_dup_u8(src);
    res = vld1_lane_u8(src + 1, res, 1);
    return res;
}

static inline uint8x8_t loadPixelsDDS_U8(const uint8_t* src)
{
    uint8x8_t res = vld1_dup_u8(src);
    res = vld1_lane_u8(src + 1, res, 1);
    res = vld1_lane_u8(src + 2, res, 2);
    res = vld1_lane_u8(src + 3, res, 3);
    return res;
}

static inline void storePixelsDD(int16_t* dst, int16x4_t data)
{
    vst1_lane_s16(dst, data, 0);
    vst1_lane_s16(dst + 1, data, 1);
}

static inline void storePixelsDD_U8(uint8_t* dst, int16x4_t data)
{
    const uint8x8_t res = vqmovun_s16(vcombine_s16(data, data));
    vst1_lane_u8(dst, res, 0);
    vst1_lane_u8(dst + 1, res, 1);
}

static inline void storePixelsDDS_U8(uint8_t* dst, int16x4_t data)
{
    const uint8x8_t res = vqmovun_s16(vcombine_s16(data, data));
    vst1_lane_u8(dst, res, 0);
    vst1_lane_u8(dst + 1, res, 1);
    vst1_lane_u8(dst + 2, res, 2);
    vst1_lane_u8(dst + 3, res, 3);
}

static inline int16x4_t loadResidualsDD(const int16_t* src) { return vld1_s16(src); }

static inline int16x4x4_t loadResidualsDDS(const int16_t* src)
{
    int16x4x4_t res;
    res.val[0] = vld1_s16(src);
    res.val[1] = vld1_s16(src + 4);
    res.val[2] = vld1_s16(src + 8);
    res.val[3] = vld1_s16(src + 12);
    return res;
}

/*------------------------------------------------------------------------------*/
/* Apply ADDs */
/*------------------------------------------------------------------------------*/

static inline void addDD_U8(const ApplyCmdBufferArgs* args)
{
    assert(!fixedPointIsSigned(args->fixedPoint));

    const int16x4_t shiftDown = vdup_n_s16(-7);
    const int16x4_t usToSOffset = vdup_n_s16(16384);
    const int16x4_t signOffsetV = vdup_n_s16(0x80);

    uint8_t* pixels = (uint8_t*)args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4_t residuals = loadResidualsDD(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDD; ++row) {
        const uint8x8_t neonPixelsU8 = loadPixelsDD_U8(pixels);

        /* val <<= shift */
        int16x4_t neonPixels = vget_low_s16(vreinterpretq_s16_u16(vshll_n_u8(neonPixelsU8, 7)));

        /* val -= 0x4000 */
        neonPixels = vsub_s16(neonPixels, usToSOffset);

        /* val += src */
        neonPixels = vqadd_s16(neonPixels, residuals);

        /* val >>= 5 */
        neonPixels = vrshl_s16(neonPixels, shiftDown);

        /* val += sign offset */
        neonPixels = vadd_s16(neonPixels, signOffsetV);

        /* Clamp to unsigned range and store */
        storePixelsDD_U8(pixels, neonPixels);

        /* Move down next 2 elements for storage. */
        residuals = vext_s16(residuals, residuals, 2);
        pixels += args->rowPixelStride;
    }
}

#define VN_ADD_CONSTANTS_U16()                            \
    const int16x4_t shiftUp = vdup_n_s16(shift);          \
    const int16x4_t shiftDown = vdup_n_s16(-shift);       \
    const int16x4_t usToSOffset = vdup_n_s16(16384);      \
    const int16x4_t signOffsetV = vdup_n_s16(signOffset); \
    const int16x4_t minV = vdup_n_s16(0);                 \
    const int16x4_t maxV = vdup_n_s16(resultMax);

static inline void addDD_UBase(const ApplyCmdBufferArgs* args, int32_t shift, int16_t signOffset,
                               int16_t resultMax)
{
    assert(!fixedPointIsSigned(args->fixedPoint));

    VN_ADD_CONSTANTS_U16()

    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4_t residuals = loadResidualsDD(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDD; ++row) {
        int16x4_t neonPixels = loadPixelsDD(pixels);

        /* val <<= shift */
        neonPixels = vshl_s16(neonPixels, shiftUp);

        /* val -= 0x4000 */
        neonPixels = vsub_s16(neonPixels, usToSOffset);

        /* val += src */
        neonPixels = vqadd_s16(neonPixels, residuals);

        /* val >>= 5 */
        neonPixels = vrshl_s16(neonPixels, shiftDown);

        /* val += sign offset */
        neonPixels = vadd_s16(neonPixels, signOffsetV);

        /* Clamp to unsigned range */
        neonPixels = vmax_s16(vmin_s16(neonPixels, maxV), minV);

        /* Store */
        storePixelsDD(pixels, neonPixels);

        /* Move down next 2 elements for storage. */
        residuals = vext_s16(residuals, residuals, 2);
        pixels += args->rowPixelStride;
    }
}

static void addDD_U10(const ApplyCmdBufferArgs* args) { addDD_UBase(args, 5, 512, 1023); }

static void addDD_U12(const ApplyCmdBufferArgs* args) { addDD_UBase(args, 3, 2048, 4095); }

static void addDD_U14(const ApplyCmdBufferArgs* args) { addDD_UBase(args, 1, 8192, 16383); }

static inline void addDD_S16(const ApplyCmdBufferArgs* args)
{
    assert(fixedPointIsSigned(args->fixedPoint));
    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4_t residuals = loadResidualsDD(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDD; ++row) {
        const int16x4_t neonPixels = loadPixelsDD(pixels);

        storePixelsDD((int16_t*)pixels, vqadd_s16(neonPixels, residuals));

        /* Move down next 2 elements. */
        residuals = vext_s16(residuals, residuals, 2);
        pixels += args->rowPixelStride;
    }
}

static inline void addDDS_U8(const ApplyCmdBufferArgs* args)
{
    assert(!fixedPointIsSigned(args->fixedPoint));

    const int16x4_t shiftDown = vdup_n_s16(-7);
    const int16x4_t usToSOffset = vdup_n_s16(16384);
    const int16x4_t signOffsetV = vdup_n_s16(0x80);

    uint8_t* pixels = (uint8_t*)args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4x4_t residuals = loadResidualsDDS(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDDS; ++row) {
        const uint8x8_t neonPixelsU8 = loadPixelsDDS_U8(pixels);

        /* val <<= shift */
        int16x4_t neonPixels = vget_low_s16(vreinterpretq_s16_u16(vshll_n_u8(neonPixelsU8, 7)));

        /* val -= 0x4000 */
        neonPixels = vsub_s16(neonPixels, usToSOffset);

        /* val += src */
        neonPixels = vqadd_s16(neonPixels, residuals.val[row]);

        /* val >>= 5 */
        neonPixels = vrshl_s16(neonPixels, shiftDown);

        /* val += sign offset */
        neonPixels = vadd_s16(neonPixels, signOffsetV);

        /* Clamp to unsigned range and store */
        storePixelsDDS_U8(pixels, neonPixels);

        pixels += args->rowPixelStride;
    }
}

static inline void addDDS_UBase(const ApplyCmdBufferArgs* args, int32_t shift, int16_t signOffset,
                                int16_t resultMax)
{
    assert(!fixedPointIsSigned(args->fixedPoint));

    VN_ADD_CONSTANTS_U16()

    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4x4_t residuals = loadResidualsDDS(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDDS; ++row) {
        /* Load as int16_t, source data is maximally unsigned 14-bit so will fit. */
        int16x4_t neonPixels = vld1_s16((const int16_t*)pixels);

        /* val <<= shift */
        neonPixels = vshl_s16(neonPixels, shiftUp);

        /* val -= 0x4000 */
        neonPixels = vsub_s16(neonPixels, usToSOffset);

        /* val += src */
        neonPixels = vqadd_s16(neonPixels, residuals.val[row]);

        /* val >>= 5 */
        neonPixels = vrshl_s16(neonPixels, shiftDown);

        /* val += sign offset */
        neonPixels = vadd_s16(neonPixels, signOffsetV);

        /* Clamp to unsigned range */
        neonPixels = vmax_s16(vmin_s16(neonPixels, maxV), minV);

        /* Store */
        vst1_s16((int16_t*)pixels, neonPixels);
        pixels += args->rowPixelStride;
    }
}

static inline void addDDS_U10(const ApplyCmdBufferArgs* args) { addDDS_UBase(args, 5, 512, 1023); }

static inline void addDDS_U12(const ApplyCmdBufferArgs* args) { addDDS_UBase(args, 3, 2048, 4095); }

static inline void addDDS_U14(const ApplyCmdBufferArgs* args)
{
    addDDS_UBase(args, 1, 8192, 16383);
}

static inline void addDDS_S16(const ApplyCmdBufferArgs* args)
{
    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    const int16x4x4_t residuals = loadResidualsDDS(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDDS; ++row) {
        const int16x4_t neonPixels = vld1_s16(pixels);
        vst1_s16(pixels, vqadd_s16(neonPixels, residuals.val[row]));
        pixels += args->rowPixelStride;
    }
}

/*------------------------------------------------------------------------------*/
/* Apply SETs */
/*------------------------------------------------------------------------------*/

static inline void setDD(const ApplyCmdBufferArgs* args)
{
    assert(fixedPointIsSigned(args->fixedPoint));

    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4_t residuals = loadResidualsDD(args->residuals);

    vst1_lane_s16(pixels, residuals, 0);
    vst1_lane_s16(pixels + 1, residuals, 1);
    vst1_lane_s16(pixels + args->rowPixelStride, residuals, 2);
    vst1_lane_s16(pixels + args->rowPixelStride + 1, residuals, 3);
}

static inline void setDDS(const ApplyCmdBufferArgs* args)
{
    assert(fixedPointIsSigned(args->fixedPoint));

    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;

    int16x4x4_t residuals = loadResidualsDDS(args->residuals);

    for (int32_t row = 0; row < CBCKTUSizeDDS; ++row) {
        vst1_s16(pixels, residuals.val[row]);
        pixels += args->rowPixelStride;
    }
}

static inline void setZeroDD(const ApplyCmdBufferArgs* args)
{
    assert(fixedPointIsSigned(args->fixedPoint));

    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4_t neonZeros = vmov_n_s16(0);

    storePixelsDD(pixels, neonZeros);
    storePixelsDD(pixels + args->rowPixelStride, neonZeros);
}

static inline void setZeroDDS(const ApplyCmdBufferArgs* args)
{
    assert(fixedPointIsSigned(args->fixedPoint));

    int16_t* pixels = args->firstSample + (args->y * args->rowPixelStride) + args->x;
    int16x4_t neonZeros = vmov_n_s16(0);

    vst1_s16(pixels, neonZeros);
    vst1_s16(pixels + args->rowPixelStride, neonZeros);
    vst1_s16(pixels + (args->rowPixelStride * 2), neonZeros);
    vst1_s16(pixels + (args->rowPixelStride * 3), neonZeros);
}

/*------------------------------------------------------------------------------*/
/* Apply CLEARs */
/*------------------------------------------------------------------------------*/

static inline void clear(const ApplyCmdBufferArgs* args)
{
    const uint16_t x = args->x;
    const uint16_t y = args->y;

    const uint16_t clearWidth = minU16(ACBKBlockSize, args->height - y);
    const uint16_t clearHeight = minU16(ACBKBlockSize, args->width - x);

    int16_t* pixels = args->firstSample + (y * args->rowPixelStride) + x;

    if (clearHeight == ACBKBlockSize && clearWidth == ACBKBlockSize) {
        int16x8x4_t neonZeros = {0};
        for (int32_t yPos = 0; yPos < ACBKBlockSize; ++yPos) {
            vst4q_s16(pixels, neonZeros);
            pixels += args->rowPixelStride;
        }
    } else {
        const size_t clearBytes = clearHeight * sizeof(int16_t);
        for (int32_t row = 0; row < clearWidth; ++row) {
            memset(pixels, 0, clearBytes);
            pixels += args->rowPixelStride;
        }
    }
}

#define cmdBufferApplicatorBlockTemplate cmdBufferApplicatorBlockNEON
#define cmdBufferApplicatorSurfaceTemplate cmdBufferApplicatorSurfaceNEON
#include "apply_cmdbuffer_applicator.h"

#else

bool cmdBufferApplicatorBlockNEON(const LdpEnhancementTile* enhancementTile, size_t entryPointIdx,
                                  const LdpPicturePlaneDesc* plane, LdpFixedPoint fixedPoint, bool highlight)
{
    VN_UNUSED_CMDBUFFER_APPLICATOR()
}

bool cmdBufferApplicatorSurfaceNEON(const LdpEnhancementTile* enhancementTile, size_t entryPointIdx,
                                    const LdpPicturePlaneDesc* plane, LdpFixedPoint fixedPoint,
                                    bool highlight)
{
    VN_UNUSED_CMDBUFFER_APPLICATOR()
}

#endif
