#include "nord101_mcu_contract.h"

#include <cstdint>

using namespace nmm::native::mcu;

int main()
{
    VoiceTransport voice;
    if(!voice.noteOn(60, 100) || voice.pitch != 0x00fe0000u || voice.velocityWord != 0x193265u || voice.gate != 0x00200000u)
        return 1;
    // This is the captured firmware negative probe: velocity-zero note-on is
    // dispatched as note-off and leaves the gate inactive.
    if(voice.noteOn(60, 0) || voice.held || voice.gate != 0)
        return 2;
    if(voice.noteOn(128, 100) || voice.held || voice.gate != 0)
        return 3;

    // Independent values transcribed from the optional 1024-frame MCU trace
    // (dsp-runtime-followup.md): X:96, target zero, shift six.  These compare
    // the implementation with the captured writer output rather than merely
    // checking that it reaches a target.
    constexpr std::uint32_t expected[] = {
        1341, 1321, 1301, 1281, 1261, 1242, 1223, 1204, 1186,
    };
    SmoothingRecord record{1362, 0, 6, true};
    for(const auto value : expected)
    {
        if(record.step() || record.current != value)
            return 4;
    }
    return 0;
}
