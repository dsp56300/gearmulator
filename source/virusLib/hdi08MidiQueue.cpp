#include "hdi08MidiQueue.h"

#include "dspSingle.h"
#include "hdi08Queue.h"

#include "dsp56kEmu/types.h"

namespace virusLib
{
	Hdi08MidiQueue::Hdi08MidiQueue(DspSingle& _dsp, Hdi08Queue& _output, const bool _useEsaiBasedTiming, const bool _isTI) : m_output(_output), m_esai(_dsp.getPeriphX().getEsai()), m_useEsaiBasedTiming(_useEsaiBasedTiming), m_isTI(_isTI)
	{
		if(_useEsaiBasedTiming)
		{
			m_esai.setCallback([this](dsp56k::Audio*)
			{
				onAudioWritten();
			}, 0);
		}
	}

	Hdi08MidiQueue::~Hdi08MidiQueue()
	{
		if(m_useEsaiBasedTiming)
			m_esai.setCallback(nullptr, 0);
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

	void Hdi08MidiQueue::sendMidiToDSP(uint8_t _a, const uint8_t _b, const uint8_t _c) const
	{
		const char flagA = m_isTI ? 0 : 1;

		m_output.writeHostFlags(flagA, 1);

		auto sendMIDItoDSP = [this](const uint8_t _midiByte)
		{
			const dsp56k::TWord word = static_cast<dsp56k::TWord>(_midiByte) << 16 | (m_isTI ? 0xffff : 0);
			m_output.writeRX(&word, 1);
		};

		const auto command = (_a & 0xf0);

		if(command == 0xf0)
		{
			// single-byte status message
			sendMIDItoDSP(_a);
		}
		else
		{
			sendMIDItoDSP(_a);
			sendMIDItoDSP(_b);

			if(command != synthLib::M_AFTERTOUCH)
				sendMIDItoDSP(_c);
		}
	}

	void Hdi08MidiQueue::onAudioWritten()
	{
		++m_numSamplesWritten;
		sendPendingMidiEvents(m_numSamplesWritten);
	}
}
