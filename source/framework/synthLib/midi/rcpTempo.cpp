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
    struct TempoPoint
    {
        std::int64_t tick;
        std::uint64_t order;
        double bpm;
    };

    const std::uint16_t tempoGraduationSteps[256] = {
        0,   255, 225, 208, 195, 186, 178, 171, 165, 160, 156, 151, 148, 144, 141, 138, 135, 132, 130, 128, 125, 123,
        121, 119, 117, 116, 114, 112, 111, 109, 108, 106, 105, 104, 102, 101, 100, 99,  98,  96,  95,  94,  93,  92,
        91,  90,  89,  88,  87,  86,  86,  85,  84,  83,  82,  81,  81,  80,  79,  78,  78,  77,  76,  76,  75,  74,
        74,  73,  72,  72,  71,  70,  70,  69,  69,  68,  67,  67,  66,  66,  65,  65,  64,  64,  63,  63,  62,  62,
        61,  61,  60,  60,  59,  59,  58,  58,  57,  57,  56,  56,  56,  55,  55,  54,  54,  53,  53,  53,  52,  52,
        51,  51,  51,  50,  50,  49,  49,  49,  48,  48,  48,  47,  47,  47,  46,  46,  45,  45,  45,  44,  44,  44,
        43,  43,  43,  42,  42,  42,  42,  41,  41,  41,  40,  40,  40,  39,  39,  39,  38,  38,  38,  38,  37,  37,
        37,  36,  36,  36,  36,  35,  35,  35,  35,  34,  34,  34,  33,  33,  33,  33,  32,  32,  32,  32,  31,  31,
        31,  31,  30,  30,  30,  30,  29,  29,  29,  29,  29,  28,  28,  28,  28,  27,  27,  27,  27,  26,  26,  26,
        26,  26,  25,  25,  25,  25,  25,  24,  24,  24,  24,  23,  23,  23,  23,  23,  22,  22,  22,  22,  22,  21,
        21,  21,  21,  21,  20,  20,  20,  20,  20,  20,  19,  19,  19,  19,  19,  18,  18,  18,  18,  18,  17,  17,
        17,  17,  17,  17,  16,  16,  16,  16,  16,  16,  15,  15,  15,  15};

    int tempoGraduationTicks(int gradation, int timeBase)
    {
        if (gradation <= 0)
            return 0;

        const auto steps = tempoGraduationSteps[static_cast<std::size_t>(gradation & 0xff)];
        return std::max(1, static_cast<int>(std::lround(steps * timeBase / 48.0)));
    }

    double tempoAtTick(std::int64_t tick, std::int64_t startTick, double startBpm, std::int64_t endTick,
                       double targetBpm)
    {
        if (endTick <= startTick || tick >= endTick)
            return targetBpm;
        if (tick <= startTick)
            return startBpm;

        const auto ratio = static_cast<double>(tick - startTick) / static_cast<double>(endTick - startTick);
        return startBpm + (targetBpm - startBpm) * ratio;
    }

    std::vector<TempoPoint> resolveG36TempoModifiers(const RcpDocument& document,
                                                     const std::vector<TempoModifier>& modifiers, std::string& error)
    {
        std::vector<TempoPoint> points;
        std::size_t generated = 0;
        for (std::size_t i = 0; i < modifiers.size(); ++i)
        {
            const auto& modifier = modifiers[i];
            if (i + 1 < modifiers.size() && modifiers[i + 1].tick == modifier.tick)
                continue;
            // A new command cancels pending graduation, including a step due at this tick.
            while (!points.empty() && points.back().tick >= modifier.tick)
                points.pop_back();
            const auto currentBpm = points.empty() ? static_cast<double>(document.tempoBpm) : points.back().bpm;
            const auto target = std::max(1.0, std::floor(document.tempoBpm * (modifier.ratio / 64.0)));
            const auto steps = tempoGraduationSteps[modifier.gradation];
            const auto addPoint = [&](int step, double bpm)
            {
                if (++generated > maximumOutputEvents)
                {
                    error = "G36 tempo map exceeded the event safety limit";
                    return false;
                }
                const auto delta = static_cast<std::int64_t>(step) * document.timeBase / 48;
                if (!canAdvance(modifier.tick, delta))
                {
                    error = "G36 tempo position overflow";
                    return false;
                }
                const auto tick = modifier.tick + delta;
                if (!points.empty() && points.back().tick == tick)
                    points.pop_back();
                points.push_back({tick, modifier.order, std::max(1.0, std::floor(bpm))});
                return true;
            };
            // rcm2smf uses integer BPM at two-tick intervals on a 48-PPQN grid.
            for (int step = 0; step < steps; step += 2)
            {
                if (!addPoint(step, currentBpm + (target - currentBpm) * step / steps))
                    return {};
            }
            if (!addPoint(steps, target))
                return {};
        }
        return points;
    }

    std::vector<TempoPoint> resolveTempoModifiers(const RcpDocument& document, std::vector<TempoModifier> modifiers,
                                                  std::string& error)
    {
        std::stable_sort(modifiers.begin(), modifiers.end(), [](const TempoModifier& a, const TempoModifier& b)
                         { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

        if (document.isG36)
            return resolveG36TempoModifiers(document, modifiers, error);

        std::vector<TempoPoint> points;
        const double currentBpm = std::max(1, document.tempoBpm);
        std::int64_t gradStartTick = 0;
        std::int64_t gradEndTick = 0;
        double gradStartBpm = currentBpm;
        double gradTargetBpm = currentBpm;
        std::int64_t generatedThrough = 0;

        const auto addGraduationUntil = [&](std::int64_t limit)
        {
            if (gradEndTick <= gradStartTick)
                return true;

            const auto end = std::min(limit, gradEndTick);
            if (end - generatedThrough > static_cast<std::int64_t>(maximumOutputEvents - points.size()))
            {
                error = "RCP tempo map exceeded the event safety limit";
                return false;
            }
            for (auto tick = generatedThrough + 1; tick <= end; ++tick)
            {
                points.push_back(
                    {tick, 0,
                     std::max(1.0, tempoAtTick(tick, gradStartTick, gradStartBpm, gradEndTick, gradTargetBpm))});
            }
            generatedThrough = std::max(generatedThrough, end);
            return true;
        };

        for (const auto& modifier : modifiers)
        {
            const auto tick = std::max<std::int64_t>(0, modifier.tick);
            if (!addGraduationUntil(tick))
                return {};
            if (points.size() >= maximumOutputEvents)
            {
                error = "RCP tempo map exceeded the event safety limit";
                return {};
            }

            const auto currentAtCommand = tempoAtTick(tick, gradStartTick, gradStartBpm, gradEndTick, gradTargetBpm);
            const auto target = std::max(1.0, document.tempoBpm * (std::max(1, modifier.ratio) / 64.0));
            const auto duration = tempoGraduationTicks(modifier.gradation, document.timeBase);

            if (duration <= 0)
            {
                points.push_back({tick, modifier.order, target});
                gradStartTick = tick;
                gradEndTick = tick;
                gradStartBpm = target;
                gradTargetBpm = target;
                generatedThrough = std::max(generatedThrough, tick);
            }
            else
            {
                points.push_back({tick, modifier.order, currentAtCommand});
                gradStartTick = tick;
                gradEndTick = tick + duration;
                gradStartBpm = currentAtCommand;
                gradTargetBpm = target;
                generatedThrough = tick;
            }
        }

        if (!addGraduationUntil(gradEndTick))
            return {};

        std::stable_sort(points.begin(), points.end(), [](const TempoPoint& a, const TempoPoint& b)
                         { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

        std::vector<TempoPoint> unique;
        for (const auto& point : points)
        {
            if (!unique.empty() && unique.back().tick == point.tick)
                unique.back() = point;
            else
                unique.push_back(point);
        }
        return unique;
    }

    class TempoTimeline
    {
    public:
        TempoTimeline(const RcpDocument& document, const std::vector<TempoModifier>& modifiers, std::string& error) :
            m_timeBase(std::max(1, document.timeBase)), m_initialBpm(std::max(1, document.tempoBpm))
        {
            const auto points = resolveTempoModifiers(document, modifiers, error);
            double seconds = 0.0;
            std::int64_t startTick = 0;
            double bpm = m_initialBpm;
            m_segments.push_back({startTick, seconds, bpm});

            for (const auto& point : points)
            {
                if (point.tick < startTick)
                    continue;

                seconds += static_cast<double>(point.tick - startTick) * 60.0 / (bpm * m_timeBase);
                startTick = point.tick;
                bpm = std::max(1.0, point.bpm);
                m_segments.push_back({startTick, seconds, bpm});
            }
        }

        double secondsAt(std::int64_t tick) const
        {
            if (tick <= 0)
                return 0.0;

            const auto it =
                std::upper_bound(m_segments.begin(), m_segments.end(), tick,
                                 [](std::int64_t value, const Segment& segment) { return value < segment.startTick; });
            const auto& segment = it == m_segments.begin() ? m_segments.front() : *(it - 1);
            return segment.startSeconds +
                static_cast<double>(tick - segment.startTick) * 60.0 / (segment.bpm * m_timeBase);
        }


    private:
        struct Segment
        {
            std::int64_t startTick;
            double startSeconds;
            double bpm;
        };

        int m_timeBase;
        double m_initialBpm;
        std::vector<Segment> m_segments;
    };

    bool applyTempo(const RcpDocument& document, const std::vector<TempoModifier>& tempoModifiers,
                    std::vector<TimedEvent>& timedEvents, std::vector<Event>& destination, std::string& error)
    {
        const TempoTimeline timeline(document, tempoModifiers, error);
        if (!error.empty())
            return false;
        for (auto& event : timedEvents)
        {
            if (!std::isfinite(timeline.secondsAt(event.tick)))
            {
                error = "RCP tempo map produced a non-finite time";
                destination.clear();
                return false;
            }

            destination.push_back({timeline.secondsAt(event.tick), std::move(event.bytes), event.port});
        }
        return true;
    }
} // namespace synthLib::midi::rcp
