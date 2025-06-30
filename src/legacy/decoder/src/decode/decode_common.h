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

#ifndef VN_LCEVC_LEGACY_DECODE_COMMON_H
#define VN_LCEVC_LEGACY_DECODE_COMMON_H

#include "common/types.h"
#include "decode/deserialiser.h"

#include <assert.h>
#include <stdint.h>

static inline void deblockResiduals(const Deblock_t* deblock, int16_t residuals[RCLayerCountDDS])
{
    /*
        Residual layer ordering as a grid:
            [ 0  1  4  5  ]
            [ 2  3  6  7  ]
            [ 8  9  12 13 ]
            [ 10 11 14 15 ]
    */
    assert(deblock->enabled);

    /* clang-format off */
	residuals[0]  = (int16_t)((deblock->corner * (uint32_t)(residuals[0])) >> 4);  /* 0, 0 */
	residuals[1]  = (int16_t)((deblock->side   * (uint32_t)(residuals[1])) >> 4);  /* 1, 0 */
	residuals[4]  = (int16_t)((deblock->side   * (uint32_t)(residuals[4])) >> 4);  /* 2, 0 */
	residuals[5]  = (int16_t)((deblock->corner * (uint32_t)(residuals[5])) >> 4);  /* 3, 0 */
	residuals[2]  = (int16_t)((deblock->side   * (uint32_t)(residuals[2])) >> 4);  /* 0, 1 */
	residuals[7]  = (int16_t)((deblock->side   * (uint32_t)(residuals[7])) >> 4);  /* 3, 1 */
	residuals[8]  = (int16_t)((deblock->side   * (uint32_t)(residuals[8])) >> 4);  /* 0, 2 */
	residuals[13] = (int16_t)((deblock->side   * (uint32_t)(residuals[13])) >> 4); /* 3, 2 */
	residuals[10] = (int16_t)((deblock->corner * (uint32_t)(residuals[10])) >> 4); /* 0, 3 */
	residuals[11] = (int16_t)((deblock->side   * (uint32_t)(residuals[11])) >> 4); /* 1, 3 */
	residuals[14] = (int16_t)((deblock->side   * (uint32_t)(residuals[14])) >> 4); /* 2, 3 */
	residuals[15] = (int16_t)((deblock->corner * (uint32_t)(residuals[15])) >> 4); /* 3, 3 */
    /* clang-format on */
}

/**! Removes user data from decoded coeffs if it is enabled.
 *
 *   In the future we may want to store this and report it to the user so they
 *   may process it.
 *
 *   Additionally, in the current decoder implementation, processing of user-data
 *   will be very expensive as it will, for each transform that has user-data,
 *   produce a transform, even if that transform is all 0's.
 */
static inline void stripUserData(LOQIndex_t loq, const UserDataConfig_t* userData, int16_t* coeffs)
{
    if ((loq == LOQ1) && userData->enabled) {
        int32_t coeff = coeffs[userData->layerIndex];
        coeff >>= userData->shift;
        const int32_t sign = (coeff & 0x01) ? -1 : 1;
        coeffs[userData->layerIndex] = (int16_t)((coeff >> 1) * sign);
    }
}

#endif // VN_LCEVC_LEGACY_DECODE_COMMON_H
