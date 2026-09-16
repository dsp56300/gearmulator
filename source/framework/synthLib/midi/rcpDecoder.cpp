// RCP/R36 V2 playback by masanaohayashi, adapted from gearmulator PR #303.
// G36 decoding adapted from shingo45endo/rcm2smf (MIT), commit
// 30e744559e554f5aa25fb7c97cdcc5ed994708e8.
/*
MIT License

Copyright (c) 2019-2026 shingo45endo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>
#include "rcpInternal.h"

namespace synthLib::midi::rcp
{
    constexpr std::size_t rcpHeaderSize = 0x586;
    constexpr std::size_t rcpTrackHeaderSize = 0x2c;
    constexpr std::size_t rcpEventSize = 4;
    constexpr std::size_t maximumTrackCount = 36;
    constexpr std::size_t maximumTrackEvents = 250000;

    const char rcpHeaderPrefix[] = "RCM-PC98V2.0(C)COME ON MUSIC";
    const char g36HeaderPrefix[] = "COME ON MUSIC RECOMPOSER RCP3.0";

    bool readLittleEndian16(const std::vector<std::uint8_t>& data, std::size_t offset, std::uint16_t& value)
    {
        if (offset > data.size() || data.size() - offset < 2)
            return false;

        value = static_cast<std::uint16_t>(data[offset]) | static_cast<std::uint16_t>(data[offset + 1] << 8);
        return true;
    }

    int signedSevenBit(std::uint8_t value)
    {
        return (value & 0x40) != 0 ? static_cast<int>(value) - 0x80 : static_cast<int>(value);
    }


    std::uint32_t decodeTrackLength(std::uint16_t encoded)
    {
        // The ordinary RCP v2 length is a multiple of four, so the low two bits
        // are unused.  Some Recomposer-compatible writers use them as bits 16/17.
        return static_cast<std::uint32_t>((encoded & ~0x03u) | ((encoded & 0x03u) << 16));
    }

    bool isRcpTrackFooter(const std::vector<std::uint8_t>& data, std::size_t offset)
    {
        return offset <= data.size() && data.size() - offset >= 4 && std::memcmp(data.data() + offset, "RCFW", 4) == 0;
    }

    bool parseRcpDocument(const std::vector<std::uint8_t>& data, RcpDocument& document, std::string& error)
    {
        const bool isG36 = document.isG36;
        const std::size_t headerSize = isG36 ? 0xc98 : rcpHeaderSize;
        const std::size_t trackHeaderSize = isG36 ? 0x2e : rcpTrackHeaderSize;
        const std::size_t eventSize = isG36 ? 6 : rcpEventSize;
        if (data.size() < headerSize)
        {
            error = "RCP header is truncated";
            return false;
        }

        const auto read16 = [&](std::size_t offset)
        {
            std::uint16_t value = 0;
            readLittleEndian16(data, offset, value);
            return value;
        };
        const auto timeBase = isG36 ? read16(0x20a) : (data[0x1c0] | (data[0x1e7] << 8));
        document.timeBase = std::max(1, static_cast<int>(timeBase));
        document.tempoBpm = std::max(1, isG36 ? static_cast<int>(read16(0x20c)) : static_cast<int>(data[0x1c1]));
        document.beatNumerator = data[isG36 ? 0x20e : 0x1c2];
        if (document.beatNumerator == 0)
            document.beatNumerator = 4;
        document.beatDenominator = data[isG36 ? 0x20f : 0x1c3];

        if (document.beatDenominator == 0 || document.beatDenominator > 32 ||
            (document.beatDenominator & (document.beatDenominator - 1)) != 0)
        {
            document.beatDenominator = 4;
        }

        document.userSysEx.clear();
        document.userSysEx.reserve(8);
        for (int i = 0; i < 8; ++i)
        {
            const auto offset = static_cast<std::size_t>((isG36 ? 0xb18 + 23 : 0x406 + 24) + i * 0x30);
            document.userSysEx.emplace_back(data.begin() + static_cast<std::ptrdiff_t>(offset),
                                            data.begin() + static_cast<std::ptrdiff_t>(offset + (isG36 ? 25 : 24)));
        }

        const auto declaredTrackCount = static_cast<std::size_t>(isG36 ? read16(0x208) : data[0x1e6]);
        if (isG36 && (declaredTrackCount == 0 || declaredTrackCount > maximumTrackCount))
        {
            error = "Invalid G36 track count";
            return false;
        }
        const bool hasExplicitTrackCount = declaredTrackCount != 0;
        // A zero track-count byte denotes the original 18-track RCP format;
        // nonzero files may explicitly contain up to 36 tracks.
        const auto trackLimit =
            std::min(maximumTrackCount, hasExplicitTrackCount ? declaredTrackCount : static_cast<std::size_t>(18));

        document.tracks.clear();
        std::size_t offset = headerSize;
        for (std::size_t trackIndex = 0; trackIndex < trackLimit; ++trackIndex)
        {
            if (offset > data.size() || data.size() - offset < trackHeaderSize)
            {
                if (!isG36)
                    break;
                error = "G36 track header is truncated";
                return false;
            }

            if (isRcpTrackFooter(data, offset))
                break;

            std::uint16_t encodedLength = 0;
            if (!readLittleEndian16(data, offset, encodedLength))
                break;

            const auto declaredLength = isG36
                ? static_cast<std::size_t>(encodedLength) | (static_cast<std::size_t>(read16(offset + 2)) << 16)
                : static_cast<std::size_t>(decodeTrackLength(encodedLength));
            if (isG36 &&
                (declaredLength < trackHeaderSize || declaredLength > data.size() - offset ||
                 (declaredLength - trackHeaderSize) % eventSize != 0))
            {
                error = "Invalid or truncated G36 track";
                return false;
            }
            if (declaredLength < trackHeaderSize)
                break;

            const auto availableLength = data.size() - offset;
            const auto trackLength = std::min(declaredLength, availableLength);
            if (trackLength < trackHeaderSize)
                break;

            RcpTrack track;
            const auto rawChannel = data[offset + (isG36 ? 6 : 4)];
            const auto channelOff = rawChannel == 0xff || (rawChannel & 0x80) != 0;
            const auto logicalChannel = static_cast<int>(rawChannel & (isG36 ? 0x7f : 0x1f));
            track.port = static_cast<std::uint8_t>(logicalChannel >> 4);
            track.channel = channelOff ? -1 : (logicalChannel & 0x0f);
            const auto rawTransposition = data[offset + (isG36 ? 7 : 5)];
            track.transposition = (rawTransposition & 0x80) != 0 ? 0
                                                                 : signedSevenBit(rawTransposition) +
                    static_cast<int>(static_cast<std::int8_t>(data[isG36 ? 0x211 : 0x1c5]));
            track.startTick = static_cast<int>(static_cast<std::int8_t>(data[offset + (isG36 ? 8 : 6)]));
            track.muted = isG36 ? (data[offset + 9] & 1) != 0 : data[offset + 7] == 1;

            const auto eventBytes = trackLength - trackHeaderSize;
            if (isG36 && eventBytes / eventSize > maximumTrackEvents)
            {
                error = "G36 track exceeded the event safety limit";
                return false;
            }
            const auto eventCount = std::min(maximumTrackEvents, eventBytes / eventSize);
            track.events.reserve(eventCount);
            for (std::size_t eventIndex = 0; eventIndex < eventCount; ++eventIndex)
            {
                const auto eventOffset = offset + trackHeaderSize + eventIndex * eventSize;
                RcpEvent event{data[eventOffset],
                               isG36 ? read16(eventOffset + 2) : static_cast<std::uint16_t>(data[eventOffset + 1]),
                               isG36 ? read16(eventOffset + 4) : static_cast<std::uint16_t>(data[eventOffset + 2]),
                               data[eventOffset + (isG36 ? 1 : 3)]};
                if (event.command == 0xfc)
                {
                    // The operand is the byte offset of the referenced measure from the start of the track,
                    // header included: G36 keeps it in the 16-bit gate field (next to the measure number in
                    // the step field), RCP v2 in the gate and velocity bytes.
                    const auto targetOffset = isG36
                        ? static_cast<std::size_t>(event.param1)
                        : static_cast<std::size_t>((event.param1 & 0xfc) | (event.param2 << 8));
                    if (targetOffset >= trackHeaderSize && (targetOffset - trackHeaderSize) % eventSize == 0)
                        event.repeatTarget = (targetOffset - trackHeaderSize) / eventSize;
                }
                if (isG36 && event.command == 0xf7)
                    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(eventOffset + 1), 5,
                                event.continuation.begin());
                track.events.push_back(event);
                if (event.command == 0xfe || event.command == 0xff)
                    break;
            }

            document.tracks.push_back(std::move(track));

            if (trackLength < declaredLength)
                break;

            offset += trackLength;
        }

        // Old RCP files sometimes store ST+ as an unsigned byte.  The converter
        // used by winrcp/rcm2smf selects the unsigned interpretation only when a
        // legacy file contains a value that cannot reasonably be signed ST+.
        const bool signedStartTicks = hasExplicitTrackCount ||
            std::all_of(document.tracks.begin(), document.tracks.end(),
                        [](const RcpTrack& track) { return track.startTick >= -99 && track.startTick <= 99; });
        if (!signedStartTicks)
        {
            for (auto& track : document.tracks)
                track.startTick = static_cast<std::uint8_t>(track.startTick);
        }

        if (document.tracks.empty())
        {
            error = "RCP contains no usable tracks";
            return false;
        }

        return true;
    }

} // namespace synthLib::midi::rcp

namespace synthLib::midi
{
    using namespace rcp;
    bool isRcpV2(const std::vector<std::uint8_t>& data) noexcept
    {
        const auto prefixLength = sizeof(rcpHeaderPrefix) - 1;
        return data.size() >= prefixLength && std::memcmp(data.data(), rcpHeaderPrefix, prefixLength) == 0;
    }

    bool readRcp(const std::vector<std::uint8_t>& data, std::vector<Event>& destination, std::string& error)
    {
        destination.clear();
        error.clear();

        const bool isG36 = data.size() >= sizeof(g36HeaderPrefix) - 1 &&
            std::memcmp(data.data(), g36HeaderPrefix, sizeof(g36HeaderPrefix) - 1) == 0;
        if (!isRcpV2(data) && !isG36)
        {
            error = "Not an RCP/R36 v2 or G36 file";
            return false;
        }

        RcpDocument document;
        document.isG36 = isG36;
        if (!parseRcpDocument(data, document, error))
            return false;

        std::vector<TimedEvent> timedEvents;
        std::vector<TempoModifier> tempoModifiers;
        std::uint64_t order = 0;

        for (std::size_t trackIndex = 0; trackIndex < document.tracks.size(); ++trackIndex)
        {
            const auto& track = document.tracks[trackIndex];
            if (track.muted)
                continue;

            std::vector<TimedEvent> trackEvents;
            if (!expandTrack(document, track, trackEvents, tempoModifiers, order, error))
            {
                error = "Track " + std::to_string(trackIndex + 1) + ": " + error;
                return false;
            }

            // R36 stores A1-A16 as 0-15 and B1-B16 as 16-31 in each track header,
            // allowing the MIDI system to be selected independently per track.
            if (trackEvents.size() > maximumOutputEvents - timedEvents.size())
            {
                error = "RCP expansion exceeded the event safety limit";
                return false;
            }
            for (auto& event : trackEvents)
            {
                timedEvents.push_back(std::move(event));
            }
        }

        if (timedEvents.empty())
        {
            error = "RCP contains no MIDI events";
            return false;
        }

        std::stable_sort(timedEvents.begin(), timedEvents.end(), [](const TimedEvent& a, const TimedEvent& b)
                         { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

        return applyTempo(document, tempoModifiers, timedEvents, destination, error);
    }
} // namespace synthLib::midi
