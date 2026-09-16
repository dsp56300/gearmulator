#include "songReset.h"

namespace synthLib::midi
{
    void appendSongReset(std::vector<SMidiEvent>& _events, ResetMode mode, uint8_t portCount, uint32_t _offset)
    {
        for (uint8_t port = 0; port < portCount; ++port)
        {
            // Stop/pause preserves expression. A fresh song must not inherit bends,
            // sustain or an unfinished controller selection from the previous file.
            for (uint8_t channel = 0; channel < 16; ++channel)
            {
                auto& event =
                    _events.emplace_back(synthLib::MidiEventSource::Host, static_cast<uint8_t>(0xb0 | channel),
                                         synthLib::MC_RESETALLCONTROLLERS, 0, _offset);
                event.port = port;
            }
            if (mode == ResetMode::Off)
                continue;
            auto& event = _events.emplace_back(synthLib::MidiEventSource::Host);
            event.offset = _offset;
            event.port = port;
            event.cancelOnTransportChange = true;
            if (mode == ResetMode::Gm)
                event.sysex = {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7};
            else
                event.sysex = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7};
        }
    }

    void appendGsMt32Arrangement(std::vector<SMidiEvent>& _events, uint8_t portCount, uint32_t _offset)
    {
        // SC-55 Owner's Manual, p.31: MT-32 arrangement for parts 1-10;
        // parts 11-16 retain their GS defaults. This does not emulate MT-32 SysEx.
        static constexpr uint8_t programs[] = {0, 68, 48, 95, 78, 41, 3, 110, 122, 127};
        static constexpr uint8_t pans[] = {64, 54, 54, 54, 54, 18, 91, 1, 127, 64};
        for (uint8_t port = 0; port < portCount; ++port)
            for (uint8_t channel = 0; channel < 10; ++channel)
            {
                const auto cc = [&](const uint8_t _controller, const uint8_t _value)
                {
                    auto& event =
                        _events.emplace_back(synthLib::MidiEventSource::Host, static_cast<uint8_t>(0xb0 | channel),
                                             _controller, _value, _offset);
                    event.port = port;
                };
                cc(0, channel == 9 ? 0 : 127);
                cc(32, 0);
                auto& event = _events.emplace_back(synthLib::MidiEventSource::Host,
                                                   static_cast<uint8_t>(0xc0 | channel), programs[channel], 0, _offset);
                event.port = port;
                cc(7, 100);
                cc(10, pans[channel]);
                cc(91, 64);
                cc(93, 0);
            }
    }
} // namespace synthLib::midi
