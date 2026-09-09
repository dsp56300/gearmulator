#include "hdi08MidiQueue.h"

#include "dspSingle.h"
#include "hdi08Queue.h"

#include "dsp56kEmu/types.h"

#include "synthLib/midiBufferParser.h"
#include "synthLib/midiTypes.h"

namespace virusLib
{
	// a quarter of the 48000 samples the Virus A watchdog counts before it fires
	static constexpr uint32_t g_midiHeartbeatSamples = 12000;

	Hdi08MidiQueue::Hdi08MidiQueue(DspSingle& _dsp, Hdi08Queue& _output, const bool _useEsaiBasedTiming, const bool _isTI, const bool _needsMidiHeartbeat) : m_output(_output), m_esai(_dsp.getAudio()), m_useEsaiBasedTiming(_useEsaiBasedTiming), m_isTI(_isTI), m_needsMidiHeartbeat(_needsMidiHeartbeat)
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
}
