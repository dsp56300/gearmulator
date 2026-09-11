#include <memory>
#include <string>
#include "cpu/common/test_util.hpp"
#include "custom_chips/xp/xp.h"

void runSc88ReleaseTests(const char*);
void runSc88VoiceReuseTests(const char*);

int main(int argc, char** argv)
{
	using namespace test;
	{
		auto xp = std::make_unique<xpLib::XP>();
		xp->hostWrite(0x3900, 1);
		(void)xp->hostRead(0x3900);
		CHECK(xp->state().voices[0].resetState_3900.released);
		xp->retireVoice(0);
		xp->hostWrite(0x3900, 3);
		(void)xp->hostRead(0x3900);
		CHECK(!xp->state().voices[0].resetState_3900.released);
		CHECK(xp->state().voices[1].resetState_3900.released);
		xp->hostWrite(0x3900, 2);
		xp->hostWrite(0x3900, 3);
		(void)xp->hostRead(0x3900);
		CHECK(xp->state().voices[0].resetState_3900.released);
		xp->retireVoice(0);
		xp->reset();
		xp->hostWrite(0x3900, 1);
		(void)xp->hostRead(0x3900);
		CHECK(xp->state().voices[0].resetState_3900.released);
	}
	{
		// Firmware can ask a retired slot to fade out before resetting/reusing
		// it. Stopping its control clock suppresses the completion IRQ forever.
		auto xp = std::make_unique<xpLib::XP>();
		xp->state().highestVoice = 0;
		xp->hostWrite(0x3900, 1);
		(void)xp->hostRead(0x3900);
		xp->retireVoice(0);
		auto& voice = xp->state().voices[0];
		voice.ampCurrent_1e00 = 0x1854a;
		xp->hostWrite(0x3918, 1 << xpLib::XP::IrqReason::ampTerminal);
		xp->hostWrite(0x1a00, 1);
		xp->hostWrite(0x1a02, 0x8040);
		for (unsigned i = 0; i < 4096 && !xp->state().interrupt; ++i)
			xp->step();
		CHECK(xp->state().interrupt);
		CHECK_EQ(xp->state().irqStatus, xpLib::XP::IrqReason::ampTerminal);
		CHECK_EQ(voice.ampCurrent_1e00, 0u);
		CHECK(!voice.resetState_3900.released);
		CHECK_EQ(voice.addressFraction_0e00, 0u);
		CHECK_EQ(voice.filterOutput_2a00, 0u);
		(void)xp->hostRead(0x391a);
		xp->hostWrite(0x3918, 1 << xpLib::XP::IrqReason::muteTransition);
		voice.waveControl_0000 = xpLib::XP::WaveControl::muteRequest | xpLib::XP::WaveControl::irqEnable;
		xp->step();
		CHECK(xp->state().interrupt);
		CHECK_EQ(xp->state().irqStatus, xpLib::XP::IrqReason::muteTransition);
		xp->hostWrite(0x3900, 0);
		const auto stoppedCounter = voice.playbackStateConfig_1000;
		xp->step();
		CHECK_EQ(voice.playbackStateConfig_1000, stoppedCounter);
		xp->hostWrite(0x3900, 1);
		(void)xp->hostRead(0x3900);
		CHECK(voice.resetState_3900.released);
	}
	{
		// A quiescent retired voice must wake on a new H8 request, including
		// the indirect register window. Its counter must never lose phase.
		auto xp = std::make_unique<xpLib::XP>();
		xp->state().highestVoice = 0;
		xp->hostWrite(0x3900, 1);
		(void)xp->hostRead(0x3900);
		xp->retireVoice(0);
		xp->hostWrite(0x3918, 1 << xpLib::XP::IrqReason::ampTerminal);
		for (unsigned i = 0; i < 256; ++i)
			xp->step();
		CHECK_EQ(xp->retiredControlVoices(), uint64_t{0});
		auto& v = xp->state().voices[0];
		const auto counter = v.playbackStateConfig_1000 & 7;
		for (unsigned i = 0; i < 13; ++i)
			xp->step();
		CHECK_EQ(v.playbackStateConfig_1000 & 7, (counter + 13) & 7);
		// Arm a new terminal event on a sleeping slot.
		xp->hostWrite(0x3934, 0);
		xp->hostWrite(0x39a8, 1);
		xp->hostWrite(0x39aa, 0);
		CHECK_EQ(xp->retiredControlVoices(), uint64_t{1});
		for (unsigned i = 0; i < 16 && !xp->interruptState(); ++i)
			xp->step();
		CHECK(xp->interruptState());
		CHECK_EQ(xp->state().irqStatus, xpLib::XP::IrqReason::ampTerminal);
		(void)xp->hostRead(0x391a);
		for (unsigned i = 0; i < 256; ++i)
			xp->step();
		CHECK_EQ(xp->retiredControlVoices(), uint64_t{0});
		// A masked terminal request must wake when its IRQ is enabled.
		xp->hostWrite(0x3918, 0);
		xp->hostWrite(0x1a00, 1);
		xp->hostWrite(0x1a02, 0);
		for (unsigned i = 0; i < 256; ++i)
			xp->step();
		CHECK_EQ(xp->retiredControlVoices(), uint64_t{0});
		xp->hostWrite(0x3918, 1 << xpLib::XP::IrqReason::ampTerminal);
		for (unsigned i = 0; i < 16 && !xp->interruptState(); ++i)
			xp->step();
		CHECK(xp->interruptState());
		xp->hostWrite(0x3900, 0);
		CHECK_EQ(xp->retiredControlVoices(), uint64_t{0});
	}
	{
		// One unacknowledged IRQ must not put another voice's request to sleep.
		auto xp = std::make_unique<xpLib::XP>();
		xp->state().highestVoice = 1;
		xp->hostWrite(0x3900, 3);
		(void)xp->hostRead(0x3900);
		xp->retireVoice(0);
		xp->retireVoice(1);
		xp->hostWrite(0x3918, 1 << xpLib::XP::IrqReason::ampTerminal);
		for (uint16_t offset : {0, 4})
		{
			xp->hostWrite(0x1a00 + offset, 1);
			xp->hostWrite(0x1a02 + offset, 0);
		}
		for (unsigned i = 0; i < 512; ++i)
			xp->step();
		CHECK(xp->interruptState());
		CHECK_EQ(xp->state().irqStatus, xpLib::XP::IrqReason::ampTerminal);
		CHECK(xp->retiredControlVoices() & uint64_t{2});
		(void)xp->hostRead(0x391a);
		for (unsigned i = 0; i < 16 && !xp->interruptState(); ++i)
			xp->step();
		CHECK(xp->interruptState());
		CHECK_EQ(xp->state().irqStatus, 0x100 | xpLib::XP::IrqReason::ampTerminal);
		(void)xp->hostRead(0x391a);
		for (unsigned i = 0; i < 256; ++i)
			xp->step();
		CHECK_EQ(xp->retiredControlVoices(), uint64_t{0});
	}

	if (argc > 1)
	{
		if (argc > 2 && std::string(argv[2]) == "--voice-reuse")
			runSc88VoiceReuseTests(argv[1]);
		else
		{
			runSc88ReleaseTests(argv[1]);
			runSc88VoiceReuseTests(argv[1]);
		}
	}
	return finish("sc88VoiceRelease");
}
