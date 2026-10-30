#include "wHardware.h"

#include "dsp56kEmu/audio.h"

#include "synthLib/midiBufferParser.h"

#include "hardwareLib/sciMidi.h"

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

		// The DSP waits for a UC that falls behind, see dspHeldByGrant, but not for one that waits for it. Set once the
		// loop waits: its condition is mostly met at once, and then nothing is locked
		bool waits = false;

		while(_continue() && !m_terminateUcThread)
		{
			if(!waits)
			{
				waits = true;
				setUcWaitsForDsp(true);
			}

			// Spin while the audio thread is actively processing a buffer -
			// latency here is critical because the audio callback is blocked
			// on DSP output and any UC sleep delays DSP progress.
			// Also spin pre-boot (m_esaiFrameIndex == 0), when nothing will
			// ever notify m_processAudioCv.
			if(m_processAudio.load(std::memory_order_acquire) || m_esaiFrameIndex == 0)
			{
				std::this_thread::yield();
			}
			else
			{
				// Idle between audio buffers: the DSP is blocked on its
				// output ring being full, so the UC's wait condition can't
				// resolve until audio processing resumes. Sleep until then.
				std::unique_lock uLock(m_processAudioMutex);
				m_processAudioCv.wait(uLock);
			}
		}

		if(waits)
			setUcWaitsForDsp(false);

		if(dspHalted)
			haltDSP();
	}

	void Hardware::requestUcTermination()
	{
		m_terminateUcThread = true;
		{
			std::lock_guard lock(m_haltDSPmutex);
			m_ucGranting = false;
		}
		m_haltDSPcv.notify_all();
		m_processAudioCv.notify_all();
	}

	void Hardware::ucThreadTerminated()
	{
		{
			std::lock_guard lock(m_haltDSPmutex);
			m_haltDSP = false;
			m_ucGranting = false;
		}
		m_haltDSPcv.notify_all();
	}

	void Hardware::grantFrame(const uint32_t _frame)
	{
		m_ucGrantedFrame = _frame;

		// the first grant turns the limit on, under the mutex so that it cannot undo requestUcTermination
		if(!m_ucGranting)
		{
			std::lock_guard lock(m_haltDSPmutex);
			m_ucGranting = !m_terminateUcThread;
		}

		wakeDspIfWaiting();
	}

	void Hardware::setUcWaitsForDsp(const bool _wait)
	{
		m_ucWaitsForDsp = _wait;
		if(_wait)
			wakeDspIfWaiting();
	}

	void Hardware::wakeDspIfWaiting()
	{
		// The DSP sets m_dspWaitsForUc before its last check, the UC stores what it changed before it reads the flag:
		// one of the two sees the other, so the UC takes the mutex only when the DSP waits. Taking it keeps the notify
		// from falling between the DSP's check and its wait. A DSP that the UC halts waits for resumeDSP instead
		if(!m_dspWaitsForUc || m_haltDSP)
			return;
		{
			std::lock_guard lock(m_haltDSPmutex);
		}
		m_haltDSPcv.notify_one();
	}

	bool Hardware::dspHeldByGrant() const
	{
		return m_ucGranting && !m_ucWaitsForDsp && m_esaiFrameIndex - m_ucGrantedFrame > g_syncHaltDspEsaiThreshold;
	}

	bool Hardware::dspMayRun() const
	{
		return !m_haltDSP && !dspHeldByGrant();
	}

	void Hardware::beginProcessAudio()
	{
		m_midiIn.refill();

		m_processAudio.store(true, std::memory_order_release);
		m_processAudioCv.notify_all();
	}

	void Hardware::endProcessAudio()
	{
		m_processAudio.store(false, std::memory_order_release);
	}

	void Hardware::sendMidi(const synthLib::SMidiEvent& _ev)
	{
		m_midiIn.push_back(_ev);
	}

	void Hardware::receiveMidi(std::vector<uint8_t>& _data)
	{
		getMidi().read(_data);
	}

	void Hardware::onEsaiCallback(dsp56k::Audio& _audio)
	{
		++m_esaiFrameIndex;

		processMidiInput();

		if((m_esaiFrameIndex & (g_syncEsaiFrameRate-1)) == 0)
			m_esaiFrameAddedCv.notify_one();

		m_processAudioCv.notify_all();

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

		if(dspMayRun())
			return;

		// the UC may wait for a frame while the DSP is about to wait for the UC: wake it, under its mutex so that the
		// wakeup cannot fall between its check and its wait
		uLock.unlock();
		{
			std::lock_guard lock(m_esaiFrameAddedMutex);
		}
		m_esaiFrameAddedCv.notify_one();
		uLock.lock();

		// set before the check in wait(), see wakeDspIfWaiting
		m_dspWaitsForUc = true;
		m_haltDSPcv.wait(uLock, [&]{ return dspMayRun(); });
		m_dspWaitsForUc = false;
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

		// one snapshot: the delta and m_lastEsaiFrameIndex must see the same frame index
		const uint32_t esaiFrameIndex = m_esaiFrameIndex.load();

		const auto ucClock = getUc().getSim().getSystemClockHz();

		const double ucCyclesPerFrame = static_cast<double>(ucClock) * m_samplerateInv;

		const auto esaiDelta = esaiFrameIndex - m_lastEsaiFrameIndex;

		// if the UC consumed more cycles than it was allowed to, remove them from remaining cycles
		m_remainingUcCyclesD += static_cast<double>(m_remainingUcCycles);

		// add cycles for the ESAI time that has passed
		m_remainingUcCyclesD += ucCyclesPerFrame * static_cast<double>(esaiDelta);

		// set new remaining cycle count
		m_remainingUcCycles = static_cast<int64_t>(m_remainingUcCyclesD);

		// and consume them
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

		grantFrame(esaiFrameIndex);
	}

	void Hardware::processMidiInput()
	{
		++m_midiOffsetCounter;

		while(!m_midiIn.empty())
		{
			const auto& e = m_midiIn.front();

			if(e.offset > m_midiOffsetCounter)
				break;

			getMidi().write(e);
			m_midiIn.pop_front();
		}
	}
}
