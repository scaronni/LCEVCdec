/* Copyright (c) V-Nova International Limited 2022-2025. All rights reserved.
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

#ifndef VN_LCEVC_LEGACY_ENTROPY_H
#define VN_LCEVC_LEGACY_ENTROPY_H

#include "common/log.h"
#include "common/memory.h"
#include "decode/deserialiser.h"
#include "decode/huffman.h"

#include <stdint.h>

/*------------------------------------------------------------------------------*/

/*! \brief temporal Huffman states. */
enum
{
    HuffTemporalZero,
    HuffTemporalOne,
    HuffTemporalCount
};

enum EntropyConstants
{
    EntropyNoData = -2
};

typedef enum EntropyDecoderType
{
    EDTDefault = 0,
    EDTTemporal,
    EDTSizeUnsigned,
    EDTSizeSigned,
    EDTCount
} EntropyDecoderType_t;

typedef struct EntropyDecoder
{
    Logger_t log;
    uint8_t currHuff;
    uint32_t rawOffset;
    HuffmanSingleDecoder_t huffman[HuffTemporalCount]; // Note that HuffTemporalCount == HuffSizeCount
    HuffmanTripleDecodeState_t comboHuffman;
    HuffmanStream_t hstream;
    bool rleOnly;
    const uint8_t* rleData;
    bool entropyEnabled;
    EntropyDecoderType_t type;
} EntropyDecoder_t;

/*------------------------------------------------------------------------------*/

/*! \brief Initialize a entropy decoder into a default state from for decompressing.
 *
 *  \param memory             context's memory manager
 *  \param log                context's logger
 *  \param state              state of the decoder to initialize
 *  \param chunk              chunk to use for this layer.
 *  \param type               specifies the type of layer decoder to prepare for this chunk.
 *  \param bitstreamVersion   Stream version (streams that lack this are treated as "current"
 *
 *  \return 0 on success, otherwise -1. */
int32_t entropyInitialise(Logger_t log, EntropyDecoder_t* state, const Chunk_t* chunk,
                          EntropyDecoderType_t type, uint8_t bitstreamVersion);

/*! \brief Decode the next coefficient from a stream. Coefficients are the things that get
 *         transformed ("Inverse hadamard transformed") to produce residuals.
 *
 *  \param state  layer decoder state to decode from
 *  \param out    s8.7 fixed point coefficient to decode into
 *
 *  \return the number of zeros following this coefficient (<0 on fail) */
int32_t ldlEntropyDecode(EntropyDecoder_t* state, int16_t* coeffOut);

/*! \brief Decode the next temporal signal from a temporal stream.
 *
 *  \param state  layer decoder state to decode from
 *  \param out    location to decode temporal signal into
 *
 *  \return the number of zeros following this pel (<0 on fail) */
int32_t ldlEntropyDecodeTemporal(EntropyDecoder_t* state, TemporalSignal_t* out);

/*! \brief Decode the next size signal from a compressed size stream.
 *
 *  \param state  layer decoder state to decode from
 *  \param size   int16_t location to decode size into
 *
 *  \return -1 on success otherwise 0. */
int32_t ldlEntropyDecodeSize(EntropyDecoder_t* state, int16_t* size);

/*! \brief Retrieve the number of bytes consumed by the entropy decoder.
 *
 *  \param state  layer decoder state.
 *
 *  \return the number of bytes read by the decoder. */
uint32_t ldlEntropyGetConsumedBytes(const EntropyDecoder_t* state);

/*------------------------------------------------------------------------------*/

#endif // VN_LCEVC_LEGACY_ENTROPY_H
