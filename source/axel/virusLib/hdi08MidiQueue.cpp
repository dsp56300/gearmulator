#include "hdi08MidiQueue.h"

#include <iterator>

#include "dspSingle.h"
#include "hdi08Queue.h"

#include "dsp56kBase/logging.h"

#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/types.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/midiTypes.h"

namespace virusLib
{
	// a quarter of the 48000 samples the Virus A watchdog counts before it fires
	static constexpr uint32_t g_midiHeartbeatSamples = 12000;

	Hdi08MidiQueue::Hdi08MidiQueue(DspSingle& _dsp, Hdi08Queue& _output, const bool _useEsaiBasedTiming, const bool _isTI, const bool _needsMidiHeartbeat) : m_output(_output), m_esai(_dsp.getAudio()), m_memory(_dsp.getMemory()), m_useEsaiBasedTiming(_useEsaiBasedTiming), m_isTI(_isTI), m_needsMidiHeartbeat(_needsMidiHeartbeat)
	{
		if(_useEsaiBasedTiming)
		{
			m_esai.setCallback([this](dsp56k::Audio*)
			{
				onAudioWritten();
			});
		}
	}

	Hdi08MidiQueue::~Hdi08MidiQueue()
	{
		if(m_useEsaiBasedTiming)
			m_esai.setCallback(nullptr);
	}

	void Hdi08MidiQueue::sendPendingMidiEvents(uint32_t _maxOffset)
	{
		while(!m_pendingMidiEvents.empty() && m_pendingMidiEvents.front().offset <= _maxOffset)
		{
			const auto& ev = m_pendingMidiEvents.front();

			sendMidiToDSP(ev.a, ev.b, ev.c);

			m_pendingMidiEvents.pop_front();
		}	
	}

	void Hdi08MidiQueue::add(const synthLib::SMidiEvent& ev)
	{
		m_pendingMidiEvents.push_back(ev);
	}

	void Hdi08MidiQueue::sendMidiToDSP(uint8_t _a, const uint8_t _b, const uint8_t _c)
	{
		m_lastMidiSample = m_numSamplesWritten;

		const char flagA = m_isTI ? 0 : 1;

		m_output.writeHostFlags(flagA, 1);

		auto sendMIDItoDSP = [this](const uint8_t _midiByte)
		{
			const dsp56k::TWord word = static_cast<dsp56k::TWord>(_midiByte) << 16 | (m_isTI ? 0xffff : 0);
			m_output.writeRX(&word, 1);
		};

		// The TI family OS takes "F5 lsb msb" in front of an event as the sample of its counter at which to play it,
		// which is how the TI keeps its timing over USB. Without one, a note starts at the beginning of the OS block
		// that parses it, 0 to 63 samples late, the timing of its DIN port. The OS parses the event in the block after
		// the current one at the latest, that block starts at m_dspTime + m_dspTimeStep, so this time is never late
		// and every event plays with the same latency (EMU-238).
		// The OS keeps a timestamp for the events behind it, so the events of one frame share one: the OS reads its
		// input once per block from a ring of 128 words, which holds a burst of 41 events then, 42 without timestamps
		if(m_dspTimeStep && m_lastTimestampFrame != m_numSamplesWritten)
		{
			const auto t = (m_dspTime + m_dspTimeStep + m_numSamplesWritten - m_dspTimeFrame) & 0x3fff;
			sendMIDItoDSP(0xf5);
			sendMIDItoDSP(t & 0x7f);
			sendMIDItoDSP(t >> 7);
			m_lastTimestampFrame = m_numSamplesWritten;
		}

		const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(_a);
		if (len >= 1)
			sendMIDItoDSP(_a);
		if (len >= 2)
			sendMIDItoDSP(_b);
		if (len >= 3)
			sendMIDItoDSP(_c);
	}

	void Hdi08MidiQueue::onAudioWritten()
	{
		++m_numSamplesWritten;
		updateDspTime();
		sendPendingMidiEvents(m_numSamplesWritten);

		// Virus A firmware arms a MIDI watchdog at boot and ticks it once per 128 samples from its
		// control tick (P:$000214). At 375 ticks (threshold $177), so 48000 samples or a little over
		// a second at the ABC sample rate of 12MHz/256, it disarms itself and kills every voice via
		// the all-notes-off routine at P:$02a797. Any incoming MIDI byte resets the counter, which on
		// hardware the front panel MCU takes care of, so do the same and send Active Sensing, which
		// the firmware ignores otherwise. A host that sends MIDI clock keeps the watchdog fed by
		// itself, one that does not loses every held note a second after the last MIDI byte.
		if(m_needsMidiHeartbeat && m_numSamplesWritten - m_lastMidiSample >= g_midiHeartbeatSamples)
			sendMidiToDSP(synthLib::M_ACTIVESENSING, 0, 0);
	}

	bool Hdi08MidiQueue::enableTimestamps()
	{
		// The OS compares a timestamp with its counter in mac y1,#7,a / asl a / move x:>counter,b / sub b,a a1,y1.
		// The counter is at x:$55865 in TI and TI2 OS 5.0.7 and 5.1.7, at x:$5581d in Snow OS 5.1.7
		constexpr dsp56k::TWord sig[] = {0x0107c2, 0x200032, 0x57f000, 0, 0x218714};

		const auto* p = m_memory.getMemAreaPtr(dsp56k::MemArea_P);

		for(dsp56k::TWord i=0; i + std::size(sig) <= m_memory.sizeP(); ++i)
		{
			if(p[i] != sig[0] || p[i+1] != sig[1] || p[i+2] != sig[2] || p[i+4] != sig[4])
				continue;

			LOG("MIDI timestamps enabled, OS sample counter at x:$" << HEX(p[i+3]));
			m_dspTimeAddr = p[i+3];
			return true;
		}

		LOG("MIDI timestamps not supported by this OS");
		return false;
	}

	void Hdi08MidiQueue::updateDspTime()
	{
		const auto addr = m_dspTimeAddr.load(std::memory_order_relaxed);

		if(!addr)
			return;

		// samples << 10, advanced by one block per OS main loop iteration
		const auto t = (m_memory.get(dsp56k::MemArea_X, addr) >> 10) & 0x3fff;

		if(t == m_dspTime)
			return;

		// The block size is known once the counter was seen advancing twice. Anything that sets the counter to another
		// value makes it unknown again until the next block
		const auto step = (t - m_dspTime) & 0x3fff;

		m_dspTimeStep = m_dspTimeFrame && step <= 512 ? step : 0;
		m_dspTime = t;
		m_dspTimeFrame = m_numSamplesWritten;
	}
}
