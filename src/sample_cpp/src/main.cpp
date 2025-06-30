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

#include <LCEVC/lcevc_dec.h>
//
#include <LCEVC/api_utility/picture_layout.h>
#include <LCEVC/utility/base_decoder.h>
#include <LCEVC/utility/check.h>
#include <LCEVC/utility/configure.h>
#include <LCEVC/utility/picture_functions.h>
#include <LCEVC/utility/timestamp.h>
//
#include <CLI/CLI.hpp>
#include <fmt/core.h>
//
#include <string>
#include <vector>

using namespace lcevc_dec::utility;

// Check if an LCEVC handle is null
template <typename H>
bool isNull(H handle)
{
    return handle.hdl == 0;
}

int main(int argc, char** argv)
{
    std::string inputFile;
    std::string outputFile;
    std::string configurationFile;
    std::string inputFormat;
    bool verbose{false};
    LCEVC_ColorFormat baseFormat{};
    uint32_t pendingLimit{0};

    CLI::App app{"LCEVCdec C++ Sample"};
    app.add_option("input", inputFile, "Input stream")->required();
    app.add_option("output", outputFile, "Output YUV")->required();
    app.add_option("configuration", configurationFile, "JSON configuration");
    app.add_option("--input-format", inputFormat, "Input stream format");
    app.add_option("-b,--base-format", baseFormat, "Base format")->default_val(LCEVC_ColorFormat_Unknown);
    app.add_option("--pending-limit", pendingLimit, "Maximum number of frames to keep pending.");
    app.add_flag("-v,--verbose", verbose, "Verbose output");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Open base decoder
    auto baseDecoder = createBaseDecoderLibAV(inputFile, inputFormat, baseFormat);

    if (!baseDecoder) {
        fmt::print("Could not open input {}\n", inputFile);
        return EXIT_FAILURE;
    }

    // Open output file
    std::unique_ptr<FILE, int (*)(FILE*)> output(fopen(outputFile.c_str(), "wb"), fclose);
    if (!output) {
        fmt::print("Could not open output {}\n", outputFile);
        return EXIT_FAILURE;
    }

    // Create and initialize LCEVC decoder
    LCEVC_DecoderHandle decoder = {};
    VN_LCEVC_CHECK(LCEVC_CreateDecoder(&decoder, LCEVC_AccelContextHandle{}));

    // Default to stdout for logs
    LCEVC_ConfigureDecoderBool(decoder, "log_stdout", true);

    // Apply an JSON config
    if (!configurationFile.empty()) {
        configureDecoderFromJson(decoder, configurationFile);
    }

    // Simple command line option for verbose logging
    if (verbose) {
        LCEVC_ConfigureDecoderInt(decoder, "log_level", 5);
    }

    VN_LCEVC_CHECK(LCEVC_InitializeDecoder(decoder));

    // Create an initial output picture - decoder will set correct description on output pictures
    LCEVC_PictureHandle outputPicture{};

    LCEVC_PictureDesc outputDesc;
    // Use 2x2 as a safe small size
    LCEVC_DefaultPictureDesc(&outputDesc, LCEVC_I420_8, 2, 2);
    VN_LCEVC_CHECK(LCEVC_AllocPicture(decoder, &outputDesc, &outputPicture));

    // Output frame counter
    uint32_t outputFrame{0};

    uint32_t pendingCount{0};
    bool synchronized{false};

    // Frame loop - consume data from base
    while (true) {
        bool baseRunning = baseDecoder->update();

        // Stop at end of stream
        if (!baseRunning && pendingCount == 0) {
            break;
        }

        // Make sure LCEVC data is sent before base frame
        if (baseDecoder->hasEnhancement()) {
            // Fetch encoded enhancement data from base decoder
            BaseDecoder::Data enhancementData;
            baseDecoder->getEnhancement(enhancementData);
            uint64_t enhancementTimestamp =
                getUniqueTimestamp(enhancementData.discontinuityCount, enhancementData.pts);

            // Try to send enhancement data into decoder.
            if (VN_LCEVC_AGAIN(LCEVC_SendDecoderEnhancementData(
                    decoder, enhancementTimestamp, enhancementData.ptr, enhancementData.size))) {
                fmt::print("SendDecoderEnhancementData: {:#08x} {}\n", enhancementData.pts,
                           enhancementData.size);
                baseDecoder->clearEnhancement();
            }
        }

        if (baseDecoder->hasImage()) {
            // Fetch raw image data from base decoder
            LCEVC_PictureHandle basePicture{};
            BaseDecoder::Data baseImage;
            baseDecoder->getImage(baseImage);
            uint64_t baseTimestamp = getUniqueTimestamp(baseImage.discontinuityCount, baseImage.pts);

            VN_LCEVC_CHECK(LCEVC_AllocPicture(decoder, &baseDecoder->description(), &basePicture));

            copyPictureFromMemory(decoder, basePicture, baseImage.ptr, baseImage.size);

            // Try to end base picture into LCEVC decoder
            if (VN_LCEVC_AGAIN(LCEVC_SendDecoderBase(decoder, baseTimestamp, basePicture, 1000000, nullptr))) {
                fmt::print("SendDecoderBase: {:#08x} {}\n", baseTimestamp, basePicture.hdl);
                baseDecoder->clearImage();
                ++pendingCount;
            }
        }

        {
            // Has decoder finished with a base picture?
            LCEVC_PictureHandle doneBasePicture;
            if (VN_LCEVC_AGAIN(LCEVC_ReceiveDecoderBase(decoder, &doneBasePicture))) {
                fmt::print("ReceiveDecoderBase: {}\n", doneBasePicture.hdl);
                VN_LCEVC_CHECK(LCEVC_FreePicture(decoder, doneBasePicture));
            }
        }

        if (!isNull(outputPicture)) {
            // Send destination picture into LCEVC decoder
            if (VN_LCEVC_AGAIN(LCEVC_SendDecoderPicture(decoder, outputPicture))) {
                fmt::print("SendDecoderPicture: {}\n", outputPicture.hdl);
                // Allocate next output
                VN_LCEVC_CHECK(LCEVC_AllocPicture(decoder, &outputDesc, &outputPicture));
            }
        }

        // Sync. LCEVC decoder if base is exhausted
        if (!synchronized && !baseRunning) {
            VN_LCEVC_CHECK(LCEVC_SynchronizeDecoder(decoder, false));
            synchronized = true;
        }

        if (synchronized || pendingCount >= pendingLimit) {
            // Has decoder produced a picture?
            LCEVC_PictureHandle decodedPicture;
            LCEVC_DecodeInformation decodeInformation;
            if (VN_LCEVC_AGAIN(LCEVC_ReceiveDecoderPicture(decoder, &decodedPicture, &decodeInformation))) {
                --pendingCount;
                LCEVC_PictureDesc desc = {0};
                VN_LCEVC_CHECK(LCEVC_GetPictureDesc(decoder, decodedPicture, &desc));
                // got output picture - write to YUV file
                fmt::print("ReceiveDecoderPicture {}: {:#08x} {} {}x{}\n", outputFrame,
                           decodeInformation.timestamp, decodedPicture.hdl, desc.width, desc.height);

                uint32_t planeCount = 0;
                VN_LCEVC_CHECK(LCEVC_GetPicturePlaneCount(decoder, decodedPicture, &planeCount));

                LCEVC_PictureLockHandle lock;
                VN_LCEVC_CHECK(LCEVC_LockPicture(decoder, decodedPicture, LCEVC_Access_Read, &lock));
                PictureLayout layout(decoder, decodedPicture);

                // Write out each row of image to output file
                for (uint32_t plane = 0; plane < planeCount; ++plane) {
                    LCEVC_PicturePlaneDesc planeDescription = {nullptr};
                    VN_LCEVC_CHECK(LCEVC_GetPictureLockPlaneDesc(decoder, lock, plane, &planeDescription));
                    for (unsigned row = 0; row < layout.planeHeight(plane); ++row) {
                        fwrite(planeDescription.firstSample +
                                   static_cast<size_t>(row * planeDescription.rowByteStride),
                               layout.rowSize(plane), 1, output.get());
                    }
                }
                VN_LCEVC_CHECK(LCEVC_UnlockPicture(decoder, lock));

                VN_LCEVC_CHECK(LCEVC_FreePicture(decoder, decodedPicture));
                outputFrame++;
            }
        }
    }

    LCEVC_DestroyDecoder(decoder);
}
