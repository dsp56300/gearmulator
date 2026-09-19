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
    constexpr size_t maximumInterpreterSteps = 4000000;
    constexpr int defaultLoopCount = 2;
    struct ActiveNote
    {
        bool active = false;
        std::int64_t offTick = 0;
        int channel = 0;
        std::uint8_t port = 0;
    };

    bool addTimedEvent(std::vector<TimedEvent>& destination, std::int64_t tick, std::uint64_t& order,
                       std::vector<std::uint8_t> bytes, std::string& error)
    {
        if (bytes.empty())
            return true;

        if (destination.size() >= maximumOutputEvents)
        {
            error = "RCP expansion exceeded the event safety limit";
            return false;
        }

        destination.push_back({std::max<std::int64_t>(0, tick), order++, 0, std::move(bytes)});
        return true;
    }

    bool addShortEvent(std::vector<TimedEvent>& destination, std::int64_t tick, int channel, std::uint8_t status,
                       std::uint8_t data1, std::uint8_t data2, std::uint64_t& order, std::string& error)
    {
        if (channel < 0 || channel >= 16)
            return true;

        const auto message =
            std::vector<std::uint8_t>{static_cast<std::uint8_t>(status | static_cast<std::uint8_t>(channel)),
                                      static_cast<std::uint8_t>(data1 & 0x7f), static_cast<std::uint8_t>(data2 & 0x7f)};
        return addTimedEvent(destination, tick, order, message, error);
    }

    bool addProgramChange(std::vector<TimedEvent>& destination, std::int64_t tick, int channel, std::uint8_t program,
                          std::uint64_t& order, std::string& error)
    {
        if (channel < 0 || channel >= 16)
            return true;

        const auto message = std::vector<std::uint8_t>{static_cast<std::uint8_t>(0xc0 | channel),
                                                       static_cast<std::uint8_t>(program & 0x7f)};
        return addTimedEvent(destination, tick, order, message, error);
    }

    std::vector<std::uint8_t> expandSysExTemplate(const std::vector<std::uint8_t>& body, std::uint8_t param1,
                                                  std::uint8_t param2, int channel)
    {
        std::vector<std::uint8_t> result;
        result.reserve(body.size() + 2);
        result.push_back(0xf0);

        int checksum = 0;
        for (const auto token : body)
        {
            int value = token;
            if (token == 0xf7)
                break;

            switch (token)
            {
            case 0x80:
                value = param1;
                break;
            case 0x81:
                value = param2;
                break;
            case 0x82:
                if (channel < 0)
                    return {};
                value = channel;
                break;
            case 0x83:
                checksum = 0;
                continue;
            case 0x84:
                value = (0x100 - checksum) & 0x7f;
                break;
            case 0xf0:
                // Definitions are specified without F0, but accepting one
                // makes files exported by older tools harmless.
                continue;
            default:
                if ((token & 0x80) != 0)
                    continue;
                break;
            }

            value &= 0x7f;
            result.push_back(static_cast<std::uint8_t>(value));
            checksum = (checksum + value) & 0x7f;
        }

        if (result.size() <= 1)
            return {};

        result.push_back(0xf7);
        return result;
    }

    bool addSysExTemplate(std::vector<TimedEvent>& destination, std::int64_t tick,
                          const std::vector<std::uint8_t>& body, std::uint8_t param1, std::uint8_t param2, int channel,
                          std::uint64_t& order, std::string& error)
    {
        if (channel < 0)
            return true;

        auto bytes = expandSysExTemplate(body, param1, param2, channel);
        if (bytes.size() <= 2)
            return true;

        return addTimedEvent(destination, tick, order, std::move(bytes), error);
    }

    std::vector<std::uint8_t> makeChannelSysEx(std::uint8_t command, int channel, std::uint8_t param1,
                                               std::uint8_t param2)
    {
        const auto deviceChannel = static_cast<std::uint8_t>(0x10 + channel);
        std::vector<std::uint8_t> bytes{0xf0, 0x43, deviceChannel};

        switch (command)
        {
        case 0xc0:
            bytes.push_back(0x08);
            break;
        case 0xc1:
            bytes.push_back(0x00);
            break;
        case 0xc2:
            bytes.push_back(0x04);
            break;
        case 0xc3:
            bytes.push_back(0x11);
            break;
        case 0xc5:
            bytes.push_back(0x15);
            break;
        case 0xc7:
            bytes.push_back(0x12);
            break;
        case 0xc8:
            bytes.push_back(0x13);
            break;
        case 0xc9:
            bytes.push_back(0x10);
            break;
        case 0xca:
            bytes.insert(bytes.end(), {0x10, 0x7b});
            break;
        case 0xcb:
            bytes.insert(bytes.end(), {0x10, 0x7c});
            break;
        case 0xcc:
            bytes.push_back(0x1b);
            break;
        case 0xcd:
            bytes.push_back(0x18);
            break;
        case 0xce:
            bytes.push_back(0x19);
            break;
        case 0xcf:
            bytes.push_back(0x1a);
            break;
        default:
            return {};
        }

        bytes.push_back(param1 & 0x7f);
        bytes.push_back(param2 & 0x7f);
        bytes.push_back(0xf7);
        return bytes;
    }

    bool addChannelSysEx(std::vector<TimedEvent>& destination, std::int64_t tick, std::uint8_t command, int channel,
                         std::uint8_t param1, std::uint8_t param2, std::uint64_t& order, std::string& error)
    {
        if (channel < 0)
            return true;

        return addTimedEvent(destination, tick, order, makeChannelSysEx(command, channel, param1, param2), error);
    }

    bool addRolandParameter(std::vector<TimedEvent>& destination, std::int64_t tick, int channel, std::uint8_t device,
                            std::uint8_t model, std::uint8_t baseHigh, std::uint8_t baseMiddle, std::uint8_t addressLow,
                            std::uint8_t parameter, std::uint64_t& order, std::string& error)
    {
        if (channel < 0)
            return true;

        const auto checksum =
            static_cast<std::uint8_t>((0x100 - ((baseHigh + baseMiddle + addressLow + parameter) & 0x7f)) & 0x7f);
        const auto bytes = std::vector<std::uint8_t>{0xf0,
                                                     0x41,
                                                     static_cast<std::uint8_t>(device & 0x7f),
                                                     static_cast<std::uint8_t>(model & 0x7f),
                                                     0x12,
                                                     static_cast<std::uint8_t>(baseHigh & 0x7f),
                                                     static_cast<std::uint8_t>(baseMiddle & 0x7f),
                                                     static_cast<std::uint8_t>(addressLow & 0x7f),
                                                     static_cast<std::uint8_t>(parameter & 0x7f),
                                                     checksum,
                                                     0xf7};
        return addTimedEvent(destination, tick, order, bytes, error);
    }

    std::size_t skipContinuationEvents(const std::vector<RcpEvent>& events, std::size_t index)
    {
        while (index < events.size() && events[index].command == 0xf7)
            ++index;
        return index;
    }

    bool repeatTargetIndex(const std::vector<RcpEvent>& events, const RcpEvent& event, std::size_t& target)
    {
        target = event.repeatTarget;
        return target < events.size();
    }

    // CVS.EXE's E7 graduation values are table indices, not milliseconds.
    class TrackExpander
    {
    public:
        TrackExpander(const RcpDocument& _document, const RcpTrack& _track, std::vector<TimedEvent>& _output,
                      std::vector<TempoModifier>& _tempo, std::uint64_t& _order, std::string& _error) :
            m_document(_document), m_track(_track), m_output(_output), m_tempo(_tempo), m_order(_order),
            m_error(_error), m_channel(_track.channel), m_port(_track.port)
        {
        }

        bool run()
        {
            if (m_track.events.empty())
                return true;

            std::size_t index = 0;
            std::vector<LoopFrame> loops;
            std::int64_t currentTick = m_track.startTick;
            std::size_t steps = 0;

            while (index < m_track.events.size())
            {
                if (++steps > maximumInterpreterSteps)
                {
                    m_error = "RCP track interpreter exceeded the safety limit";
                    return false;
                }

                if (!flushDueNotes(currentTick))
                    return false;

                const auto& event = m_track.events[index];
                const auto command = event.command;

                if (command < 0x80)
                {
                    if (!emitNote(currentTick, event))
                        return false;

                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    continue;
                }

                const auto outputStart = m_output.size();
                const auto port = m_port;
                switch (command)
                {
                case 0x90:
                case 0x91:
                case 0x92:
                case 0x93:
                case 0x94:
                case 0x95:
                case 0x96:
                case 0x97:
                    if (!addSysExTemplate(m_output, currentTick, m_document.userSysEx[command - 0x90], event.param1,
                                          event.param2, m_channel, m_order, m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0x98:
                    {
                        std::vector<std::uint8_t> body;
                        // An RCP v2 Tr.Excl record only supplies its step time
                        // and two control operands.  Its SysEx payload begins in
                        // the following F7 continuation records, just as it does
                        // in G36.  Treating param1/param2 as payload prefixes the
                        // message with invalid bytes (for example F0 02 00 41...),
                        // so GS devices discard the setup write.
                        auto next = index + 1;
                        while (next < m_track.events.size() && m_track.events[next].command == 0xf7)
                        {
                            if (m_document.isG36)
                            {
                                const auto& bytes = m_track.events[next].continuation;
                                body.insert(body.end(), bytes.begin(), bytes.end());
                            }
                            else
                            {
                                body.push_back(static_cast<std::uint8_t>(m_track.events[next].param1));
                                body.push_back(m_track.events[next].param2);
                            }
                            ++next;
                        }

                        if (!addSysExTemplate(m_output, currentTick, body, event.param1, event.param2, m_channel,
                                              m_order, m_error))
                            return false;
                        if (!advance(currentTick, event.delay))
                            return false;
                        index = next;
                        break;
                    }

                case 0x99:
                    if (!advance(currentTick, event.delay))
                        return false;
                    index = skipContinuationEvents(m_track.events, index + 1);
                    break;

                case 0xc0:
                case 0xc1:
                case 0xc2:
                case 0xc3:
                case 0xc5:
                case 0xc6:
                case 0xc7:
                case 0xc8:
                case 0xc9:
                case 0xca:
                case 0xcb:
                case 0xcc:
                case 0xcd:
                case 0xce:
                case 0xcf:
                    if (m_channel >= 0 && command == 0xc6)
                    {
                        if (!addTimedEvent(m_output, currentTick, m_order,
                                           {0xf0, 0x43, 0x75, static_cast<std::uint8_t>(m_channel & 0x7f), 0x10,
                                            static_cast<std::uint8_t>(event.param1 & 0x7f),
                                            static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
                                           m_error))
                            return false;
                    }
                    else if (!addChannelSysEx(m_output, currentTick, command, m_channel, event.param1, event.param2,
                                              m_order, m_error))
                    {
                        return false;
                    }

                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xd0:
                    m_yamahaBaseHigh = event.param1;
                    m_yamahaBaseMiddle = event.param2;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xd1:
                    m_yamahaDevice = event.param1;
                    m_yamahaModel = event.param2;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xd2:
                    if (m_channel >= 0 &&
                        !addTimedEvent(m_output, currentTick, m_order,
                                       {0xf0, 0x43, static_cast<std::uint8_t>(m_yamahaDevice & 0x7f),
                                        static_cast<std::uint8_t>(m_yamahaModel & 0x7f),
                                        static_cast<std::uint8_t>(m_yamahaBaseHigh & 0x7f),
                                        static_cast<std::uint8_t>(m_yamahaBaseMiddle & 0x7f),
                                        static_cast<std::uint8_t>(event.param1 & 0x7f),
                                        static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
                                       m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xd3:
                    if (m_channel >= 0 &&
                        !addTimedEvent(m_output, currentTick, m_order,
                                       {0xf0, 0x43, 0x10, 0x4c, static_cast<std::uint8_t>(m_yamahaBaseHigh & 0x7f),
                                        static_cast<std::uint8_t>(m_yamahaBaseMiddle & 0x7f),
                                        static_cast<std::uint8_t>(event.param1 & 0x7f),
                                        static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
                                       m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xdc:
                    if (m_channel >= 0 &&
                        !addTimedEvent(m_output, currentTick, m_order,
                                       {0xf0, 0x41, 0x32, static_cast<std::uint8_t>(m_channel & 0x7f),
                                        static_cast<std::uint8_t>(event.param1 & 0x7f),
                                        static_cast<std::uint8_t>(event.param2 & 0x7f), 0xf7},
                                       m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xdd:
                    m_rolandBaseHigh = event.param1;
                    m_rolandBaseMiddle = event.param2;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xde:
                    if (!addRolandParameter(m_output, currentTick, m_channel, m_rolandDevice, m_rolandModel,
                                            m_rolandBaseHigh, m_rolandBaseMiddle, event.param1, event.param2, m_order,
                                            m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xdf:
                    m_rolandDevice = event.param1;
                    m_rolandModel = event.param2;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xe1:
                    if (!addShortEvent(m_output, currentTick, m_channel, 0xb0, 32, event.param2, m_order, m_error) ||
                        !addProgramChange(m_output, currentTick, m_channel, event.param1, m_order, m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xe2:
                    if (!addShortEvent(m_output, currentTick, m_channel, 0xb0, 0, event.param2, m_order, m_error) ||
                        !addShortEvent(m_output, currentTick, m_channel, 0xb0, 32, 0, m_order, m_error) ||
                        !addProgramChange(m_output, currentTick, m_channel, event.param1, m_order, m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xe5:
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xe6:
                    {
                        const auto decoded = static_cast<int>(event.param1) - 1;
                        m_channel = (event.param1 == 0 || (decoded & 0x80) != 0) ? -1 : decoded & 0x0f;
                        if (m_channel >= 0)
                            m_port = static_cast<std::uint8_t>(decoded >> 4);
                        if (!advance(currentTick, event.delay))
                            return false;
                        ++index;
                        break;
                    }

                case 0xe7:
                    if (m_tempo.size() >= maximumOutputEvents)
                    {
                        m_error = "RCP tempo map exceeded the event safety limit";
                        return false;
                    }
                    m_tempo.push_back({std::max<std::int64_t>(0, currentTick), m_order++,
                                       std::max(1, static_cast<int>(event.param1)), static_cast<int>(event.param2)});
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xea:
                    if (!addShortEvent(m_output, currentTick, m_channel, 0xd0, event.param1, 0, m_order, m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xeb:
                    if (!addShortEvent(m_output, currentTick, m_channel, 0xb0, event.param1, event.param2, m_order,
                                       m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xec:
                    if (event.param1 < 0x80)
                    {
                        if (!addProgramChange(m_output, currentTick, m_channel, event.param1, m_order, m_error))
                            return false;
                    }
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xed:
                    if (!addShortEvent(m_output, currentTick, m_channel, 0xa0, event.param1, event.param2, m_order,
                                       m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xee:
                    if (!addShortEvent(m_output, currentTick, m_channel, 0xe0, event.param1, event.param2, m_order,
                                       m_error))
                        return false;
                    if (!advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;

                case 0xf5:
                    ++index;
                    break;

                case 0xf6:
                    index = skipContinuationEvents(m_track.events, index + 1);
                    break;

                case 0xf7:
                    ++index;
                    break;

                case 0xf8:
                    if (!handleLoopEnd(loops, index, event))
                        return false;
                    break;

                case 0xf9:
                    if (loops.size() < 8)
                        loops.push_back({index + 1, 0});
                    ++index;
                    break;

                case 0xfc:
                    if (!handleRepeatMeasure(index, event))
                        return false;
                    break;

                case 0xfd:
                    if (m_repeatReturnIndex != noIndex)
                    {
                        index = m_repeatReturnIndex;
                        m_repeatReturnIndex = noIndex;
                    }
                    else
                    {
                        ++index;
                    }
                    break;

                case 0xfe:
                    if (m_document.isG36 && m_repeatReturnIndex != noIndex)
                    {
                        // A reference to the final measure returns after its notes finish.
                        for (const auto& [key, note] : m_activeNotes)
                            if (note.active)
                                currentTick = std::max(currentTick, note.offTick);
                        index = m_repeatReturnIndex;
                        m_repeatReturnIndex = noIndex;
                    }
                    else
                        index = m_track.events.size();
                    break;

                default:
                    // Commands below F5 carry the normal delay field.  The
                    // high command range is timing-free, like the documented
                    // RCP meta commands.
                    if (command < 0xf5 && !advance(currentTick, event.delay))
                        return false;
                    ++index;
                    break;
                }

                for (auto i = outputStart; i < m_output.size(); ++i)
                    m_output[i].port = port;
            }

            if (!flushDueNotes(std::numeric_limits<std::int64_t>::max()))
                return false;
            return true;
        }

    private:
        struct LoopFrame
        {
            std::size_t startIndex;
            int completedPasses;
        };

        static constexpr std::size_t noIndex = std::numeric_limits<std::size_t>::max();

        bool advance(std::int64_t& tick, std::uint16_t amount)
        {
            if (!canAdvance(tick, amount))
            {
                m_error = "RCP tick position overflow";
                return false;
            }

            tick += amount;
            return true;
        }

        bool flushDueNotes(std::int64_t tick)
        {
            for (auto& [key, note] : m_activeNotes)
            {
                if (note.active && note.offTick <= tick)
                {
                    if (!addShortEvent(m_output, note.offTick, note.channel, 0x80,
                                       static_cast<std::uint8_t>(key & 0x7f), 0, m_order, m_error))
                        return false;
                    m_output.back().port = note.port;
                    note.active = false;
                }
            }
            return true;
        }

        bool emitNote(std::int64_t tick, const RcpEvent& event)
        {
            const auto noteValue = static_cast<int>(event.command) + m_track.transposition;
            if (noteValue < 0 || noteValue > 127 || event.param1 == 0 || event.param2 == 0)
                return true;

            if (m_channel < 0)
                return true;
            const auto key = static_cast<std::uint16_t>((m_port << 11) | (m_channel << 7) | noteValue);
            auto& active = m_activeNotes[key];
            if (active.active)
            {
                if (!canAdvance(tick, event.param1))
                {
                    m_error = "RCP note duration overflow";
                    return false;
                }
                active.offTick = tick + event.param1;
                return true;
            }

            if (!addShortEvent(m_output, tick, m_channel, 0x90, static_cast<std::uint8_t>(noteValue), event.param2,
                               m_order, m_error))
                return false;

            if (!canAdvance(tick, event.param1))
            {
                m_error = "RCP note duration overflow";
                return false;
            }

            active.active = true;
            active.offTick = tick + event.param1;
            active.channel = m_channel;
            active.port = m_port;
            m_output.back().port = m_port;
            return true;
        }

        bool handleLoopEnd(std::vector<LoopFrame>& loops, std::size_t& index, const RcpEvent& event)
        {
            if (loops.empty())
            {
                ++index;
                return true;
            }

            auto& frame = loops.back();
            ++frame.completedPasses;

            const auto requested = static_cast<int>(event.delay);
            const auto targetPasses =
                (requested == 0 || (!m_document.isG36 && requested >= 0x7f)) ? defaultLoopCount : requested;
            if (frame.completedPasses < targetPasses)
            {
                index = frame.startIndex;
            }
            else
            {
                loops.pop_back();
                ++index;
            }

            return true;
        }

        bool handleRepeatMeasure(std::size_t& index, const RcpEvent& event)
        {
            if (m_repeatReturnIndex != noIndex)
            {
                index = m_repeatReturnIndex;
                m_repeatReturnIndex = noIndex;
                return true;
            }

            std::size_t target = noIndex;
            if (!repeatTargetIndex(m_track.events, event, target))
            {
                if (m_document.isG36)
                {
                    m_error = "Invalid G36 repeated-measure reference at event " + std::to_string(index + 1);
                    return false;
                }
                ++index;
                return true;
            }

            // Follow FC -> FC chains before entering the repeated measure.  This
            // is how Recomposer represents a measure built from earlier measures.
            for (int guard = 0; guard < 64 && target < m_track.events.size() && m_track.events[target].command == 0xfc;
                 ++guard)
            {
                std::size_t chainedTarget = noIndex;
                if (!repeatTargetIndex(m_track.events, m_track.events[target], chainedTarget) ||
                    chainedTarget == target)
                {
                    if (m_document.isG36)
                    {
                        m_error = "Invalid G36 repeated-measure chain";
                        return false;
                    }
                    ++index;
                    return true;
                }
                target = chainedTarget;
            }

            if (m_document.isG36 && target < m_track.events.size() && m_track.events[target].command == 0xfc)
            {
                m_error = "G36 repeated-measure chain exceeded the safety limit";
                return false;
            }

            if (target >= m_track.events.size())
            {
                ++index;
                return true;
            }

            m_repeatReturnIndex = index + 1;
            index = target;
            return true;
        }

        const RcpDocument& m_document;
        const RcpTrack& m_track;
        std::vector<TimedEvent>& m_output;
        std::vector<TempoModifier>& m_tempo;
        std::uint64_t& m_order;
        std::string& m_error;

        int m_channel = -1;
        std::uint8_t m_port = 0;
        std::uint8_t m_yamahaDevice = 0x10;
        std::uint8_t m_yamahaModel = 0x4c;
        std::uint8_t m_yamahaBaseHigh = 0;
        std::uint8_t m_yamahaBaseMiddle = 0;
        std::uint8_t m_rolandDevice = 0x10;
        std::uint8_t m_rolandModel = 0x16;
        std::uint8_t m_rolandBaseHigh = 0;
        std::uint8_t m_rolandBaseMiddle = 0x10;
        std::size_t m_repeatReturnIndex = noIndex;
        std::map<std::uint16_t, ActiveNote> m_activeNotes;
    };


    bool expandTrack(const RcpDocument& document, const RcpTrack& track, std::vector<TimedEvent>& output,
                     std::vector<TempoModifier>& tempo, uint64_t& order, std::string& error)
    {
        return TrackExpander(document, track, output, tempo, order, error).run();
    }
} // namespace synthLib::midi::rcp
