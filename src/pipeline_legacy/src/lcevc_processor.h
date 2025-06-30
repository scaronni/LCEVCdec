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

#ifndef VN_LCEVC_PIPELINE_LEGACY_LCEVC_PROCESSOR_H
#define VN_LCEVC_PIPELINE_LEGACY_LCEVC_PROCESSOR_H

#include <LCEVC/common/memory.h>
#include <LCEVC/legacy/PerseusDecoder.h>
//
#include "picture.h"
//
#include <cstdint>
#include <map>
#include <memory>

// ------------------------------------------------------------------------------------------------

using LCEVCContainer = struct LCEVCContainer;

namespace lcevc_dec::decoder {

// - LCEVCProcessor -------------------------------------------------------------------------------

class LcevcProcessor
{
public:
    LcevcProcessor(perseus_decoder& decoder, BufferManager& bufferManager);

    bool initialise(uint32_t unprocessedLcevcCap, int32_t residualSurfaceFPSetting);
    void release();

    void flush();

    // Unprocessed in, processed out (return is an LCEVC_ReturnCode)
    LdcReturnCode insertUnprocessedLcevcData(const uint8_t* data, uint32_t byteSize,
                                             uint64_t timestamp, uint64_t inputTime);
    std::shared_ptr<perseus_decoder_stream> extractProcessedLcevcData(uint64_t timestamp,
                                                                      bool discardProcessed = true);

    uint32_t getUnprocessedCapacity() const;
    bool isUnprocessedQueueFull() const;

private:
    bool accumulateTemporalFromSkippedFrame(const perseus_decoder_stream& processedLcevcData);
    std::shared_ptr<perseus_decoder_stream> processUpToTimestamp(uint64_t timestamp, bool discardProcessed);
    std::shared_ptr<perseus_decoder_stream> processUpToTimestampLoop(uint64_t timestamp,
                                                                     uint32_t& numProcessedOut,
                                                                     uint64_t& lastExtractedTS,
                                                                     bool discardProcessed);

    void setLiveDecoderConfig(const perseus_global_config& globalConfig) const;

    // Core parser (so long as the Core Decoder is strictly stateful, this is simply a reference to
    // the Decoder's Core decoder)
    perseus_decoder& m_coreDecoderRef;

    // Input holder
    LdcMemoryAllocation allocation = {0};
    LCEVCContainer* m_unprocessedLcevcContainer = nullptr;

    // Output holder (only needed if peeking ahead)
    std::map<uint64_t, std::shared_ptr<perseus_decoder_stream>> m_processedLcevcContainer;

    // Picture with no data (accumulates temporal when skipping)
    PictureManaged m_skipTemporalAccumulator;

    // Config (set in initialise, not constructor, so can't be const)
    // This is "pss_surface_fp_setting" (see overview.rst - Configuration Options)
    int32_t m_residualSurfaceFPSetting = -1;
};

} // namespace lcevc_dec::decoder
#endif // VN_LCEVC_PIPELINE_LEGACY_LCEVC_PROCESSOR_H
