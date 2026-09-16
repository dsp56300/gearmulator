#pragma once
#include <array>
#include <cstddef>
#include <limits>
#include "midiFile.h"

namespace synthLib::midi::rcp
{
    inline constexpr size_t maximumOutputEvents = 2000000;
    struct RcpEvent
    {
        std::uint8_t command;
        std::uint16_t delay;
        std::uint16_t param1;
        std::uint8_t param2;
        std::size_t repeatTarget = std::numeric_limits<std::size_t>::max();
        std::array<std::uint8_t, 5> continuation{};
    };

    struct RcpTrack
    {
        int channel = -1;
        std::uint8_t port = 0;
        int transposition = 0;
        int startTick = 0;
        bool muted = false;
        std::vector<RcpEvent> events;
    };

    struct RcpDocument
    {
        bool isG36 = false;
        int timeBase = 48;
        int tempoBpm = 120;
        int beatNumerator = 4;
        int beatDenominator = 4;
        std::vector<std::vector<std::uint8_t>> userSysEx;
        std::vector<RcpTrack> tracks;
    };

    struct TimedEvent
    {
        std::int64_t tick;
        std::uint64_t order;
        std::uint8_t port = 0;
        std::vector<std::uint8_t> bytes;
    };

    struct TempoModifier
    {
        std::int64_t tick;
        std::uint64_t order;
        int ratio;
        int gradation;
    };


    inline bool canAdvance(int64_t value, int64_t amount)
    {
        return amount <= 0 || value <= std::numeric_limits<int64_t>::max() - amount;
    }
    bool expandTrack(const RcpDocument&, const RcpTrack&, std::vector<TimedEvent>&, std::vector<TempoModifier>&,
                     uint64_t&, std::string&);
    bool applyTempo(const RcpDocument&, const std::vector<TempoModifier>&, std::vector<TimedEvent>&,
                    std::vector<Event>&, std::string&);
} // namespace synthLib::midi::rcp
