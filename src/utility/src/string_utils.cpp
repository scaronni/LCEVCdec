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

// Several utility functions that any sane string library would provide.
//
#include <fmt/core.h>
#include <LCEVC/utility/string_utils.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ------------------------------------------------------------------------------------------------

namespace lcevc_dec::utility {

bool iEquals(std::string_view a, std::string_view b)
{
    static const auto toUpperEqual = [](const char c1, const char c2) {
        return std::toupper(c1) == std::toupper(c2);
    };
    return std::equal(a.begin(), a.end(), b.begin(), toUpperEqual);
}

bool startsWith(std::string_view str, std::string_view prefix)
{
    if (prefix.length() > str.length()) {
        return false;
    }
    return std::equal(str.begin(), str.begin() + prefix.length(), prefix.begin());
}

bool endsWith(std::string_view str, std::string_view suffix)
{
    if (suffix.length() > str.length()) {
        return false;
    }
    return std::equal(str.rbegin(), str.rbegin() + suffix.length(), suffix.rbegin());
}

std::vector<std::string> split(std::string_view src, std::string_view separators)
{
    std::vector<std::string> output;

    // Detect an empty input
    if (src.empty()) {
        return output;
    }

    std::string_view::const_iterator start = src.begin();
    bool inToken = true;

    for (std::string_view::const_iterator i = src.begin(); i != src.end(); ++i) {
        if (separators.find_first_of(*i) == std::string::npos) {
            // Not a separator char
            inToken = true;
        } else {
            // A separator char
            if (inToken) {
                // End of token - push from start to here to output
                output.emplace_back(start, i);
            }
            start = i + 1;
            inToken = false;
        }
    }

    // Add last token
    output.emplace_back(start, src.end());

    return output;
}

std::string hexDump(const uint8_t* data, uint32_t size, uint32_t offset, bool humanReadable)
{
    const int bytesPerLine = 16;
    const int outputCharsPerLine = 13;
    const int outputCharsByte = 4;
    std::string result;
    result.reserve(size * outputCharsByte + (size / bytesPerLine) * outputCharsPerLine);

    for (uint64_t line = 0; line < size; line += bytesPerLine) {
        if (humanReadable) {
            result += fmt::format("{:#06x} : ", offset + line);
        }
        // Bytes
        for (uint64_t byte = 0; byte < bytesPerLine; ++byte) {
            if (line + byte < size) {
                if (humanReadable) {
                    result += fmt::format("{:02x} ", data[line + byte]);
                } else {
                    result += fmt::format("0x{:02x}, ", data[line + byte]);
                }
            } else {
                result += "-- ";
            }
        }

        if (humanReadable) {
            result += " : ";

            // Chars
            for (uint64_t byte = 0; byte < bytesPerLine; ++byte) {
                if (line + byte < size) {
                    const char chr = static_cast<char>(data[line + byte]);
                    result.push_back(isprint(chr) ? chr : '.');
                }
            }
        }

        result.push_back('\n');
    }

    return result;
}

} // namespace lcevc_dec::utility
