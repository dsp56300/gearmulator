#include "nord101_mcu_contract.h"
#include "nord101_reference_state.h"

#include <cstdio>

using namespace nmm::native::mcu;

static_assert(pitchWord(60) == 0x00fe0000u);
static_assert(pitchWord(72) == 0x00040000u);
static_assert(gateWord(true) == 0x00200000u);
static_assert(gateWord(false) == 0u);
static_assert(velocityWordForMidi(1) == 0x004081u);
static_assert(velocityWordForMidi(100) == 0x193265u);
static_assert(velocityWordForMidi(127) == 0x200000u);
static_assert(kOscA1Parameters[0] == 64 && kOscA1Parameters[4] == 3);
static_assert(kFilterF1Parameters[3] == 127);
static_assert(k101PolySampleX[0].value == 0x400000u);
static_assert(k101PolyControlX[3].value == 0x2aaaabu);
static_assert(k101PolyControlY[6].value == 0x81u);
static_assert(k101OutputCoefficient == 0x16605u);
static_assert(findReadyWord(k101ReadyX, 0x60) == 0x400000u);
static_assert(findReadyWord(k101ReadyX, 0x73) == 0x400000u);
static_assert(findReadyWord(k101ReadyY, 0x5f) == 0x00d297u);
static_assert(k101GraphCursors.r3AtSampleGraph == 0x60 && k101GraphCursors.r4AtSampleGraph == 0x60);
static_assert(k101GraphCursors.n2 == 0x7bc && k101GraphCursors.n5 == 0x780 && k101GraphCursors.r6 == 0x620);
static_assert(findReadyWord(k101SharedTableX, 0x780) == 0x001000u);
static_assert(findReadyWord(k101SharedTableX, 0x7ff) == 0x5fe443u);
static_assert(findReadyWord(k101SharedTableY, 0x780) == 0x400000u);
static_assert(findReadyWord(k101SharedTableY, 0x7ff) == 0x43c669u);

int main()
{
    VoiceTransport voice;
    voice.noteOn(60, 100);
    if(voice.pitch != 0x00fe0000u || voice.velocityWord != 0x193265u || voice.gate != 0x00200000u || !voice.held)
    {
        std::fprintf(stderr, "voice note-on transport mismatch\n");
        return 1;
    }
    voice.noteOff();
    if(voice.gate != 0 || voice.held)
    {
        std::fprintf(stderr, "voice note-off transport mismatch\n");
        return 2;
    }

    SmoothingRecord record{0x00000000u, 0x00001000u, 6, true};
    for(int i = 0; i != 256 && record.active; ++i)
        record.step();
    if(record.active || record.current != record.target)
        std::fprintf(stderr, "smoother did not settle: current=%08x target=%08x\n", record.current, record.target);
    return record.active || record.current != record.target;
}
