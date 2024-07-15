#include "wHardware.h"

#include "wMidi.h"
#include "dsp56kEmu/audio.h"

#include "synthLib/midiBufferParser.h"

#include "mc68k/mc68k.h"

namespace wLib
{
	constexpr uint32_t g_syncEsaiFrameRate = 8;
	constexpr uint32_t g_syncHaltDspEsaiThreshold = 16;

	static_assert((g_syncEsaiFrameRate & (g_syncEsaiFrameRate - 1)) == 0, "esai frame sync rate must be power of two");
	static_assert(g_syncHaltDspEsaiThreshold >= g_syncEsaiFrameRate * 2, "esai DSP halt threshold must be greater than two times the sync rate");

	Hardware::Hardware(const double& _samplerate) : m_samplerateInv(1.0 / _samplerate)
	{
	}

	void Hardware::haltDSP()
	{
		if(m_haltDSP)
			return;

		std::lock_guard uLockHalt(m_haltDSPmutex);
		m_haltDSP = true;
	}

	void Hardware::resumeDSP()
	{
		if(!m_haltDSP)
			return;

		{
			std::lock_guard uLockHalt(m_haltDSPmutex);
			m_haltDSP = false;
		}
		m_haltDSPcv.notify_one();
	}

	void Hardware::ucYieldLoop(const std::function<bool()>& _continue)
	{
		const auto dspHalted = m_haltDSP;

		resumeDSP();

		while(_continue())
		{
			if(m_processAudio)
			{
				std::this_thread::yield();
			}
			else
			{
				std::unique_lock uLock(m_esaiFrameAddedMutex);
				m_esaiFrameAddedCv.wait(uLock);
			}
		}

		if(dspHalted)
			haltDSP();
	}

	void Hardware::sendMidi(const synthLib::SMidiEvent& _ev)
	{
		m_midiIn.push_back(_ev);
	}

	void Hardware::receiveMidi(std::vector<uint8_t>& _data)
	{
		getMidi().readTransmitBuffer(_data);
	}

	void Hardware::onEsaiCallback(dsp56k::Audio& _audio)
	{
		++m_esaiFrameIndex;

		processMidiInput();

		if((m_esaiFrameIndex & (g_syncEsaiFrameRate-1)) == 0)
			m_esaiFrameAddedCv.notify_one();

		m_requestedFramesAvailableMutex.lock();

		if(m_requestedFrames && _audio.getAudioOutputs().size() >= m_requestedFrames)
		{
			m_requestedFramesAvailableMutex.unlock();
			m_requestedFramesAvailableCv.notify_one();
		}
		else
		{
			m_requestedFramesAvailableMutex.unlock();
		}

		std::unique_lock uLock(m_haltDSPmutex);
		m_haltDSPcv.wait(uLock, [&]{ return m_haltDSP == false; });
	}

	void Hardware::syncUcToDSP()
	{
		if(m_remainingUcCycles > 0)
			return;

		// we can only use ESAI to clock the uc once it has been enabled
		if(m_esaiFrameIndex <= 0)
			return;

		if(m_esaiFrameIndex == m_lastEsaiFrameIndex)
		{
			resumeDSP();
			std::unique_lock uLock(m_esaiFrameAddedMutex);
			m_esaiFrameAddedCv.wait(uLock, [this]{return m_esaiFrameIndex > m_lastEsaiFrameIndex;});
		}

		const auto esaiFrameIndex = m_esaiFrameIndex;

		const auto ucClock = getUc().getSim().getSystemClockHz();

		const double ucCyclesPerFrame = static_cast<double>(ucClock) * m_samplerateInv;

		const auto esaiDelta = esaiFrameIndex - m_lastEsaiFrameIndex;

		m_remainingUcCyclesD += ucCyclesPerFrame * static_cast<double>(esaiDelta);
		m_remainingUcCycles = static_cast<int64_t>(m_remainingUcCyclesD);
		m_remainingUcCyclesD -= static_cast<double>(m_remainingUcCycles);

		if(esaiDelta > g_syncHaltDspEsaiThreshold)
		{
			haltDSP();
		}
		else
		{
			resumeDSP();
		}

		m_lastEsaiFrameIndex = esaiFrameIndex;
	}

	void Hardware::processMidiInput()
	{
		++m_midiOffsetCounter;

		while(!m_midiIn.empty())
		{
			const auto& e = m_midiIn.front();

			if(e.offset > m_midiOffsetCounter)
				break;

			if(!e.sysex.empty())
			{
				getMidi().writeMidi(e.sysex);
			}
			else
			{
				getMidi().writeMidi(e.a);
				const auto len = synthLib::MidiBufferParser::lengthFromStatusByte(e.a);
				if (len > 1)
					getMidi().writeMidi(e.b);
				if (len > 2)
					getMidi().writeMidi(e.c);
			}

			m_midiIn.pop_front();
		}
	}
}
