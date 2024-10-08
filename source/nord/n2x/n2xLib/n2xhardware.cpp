#include "n2xhardware.h"

#include "n2xromloader.h"
#include "dsp56kEmu/threadtools.h"
#include "synthLib/deviceException.h"

namespace n2x
{
	constexpr uint32_t g_syncEsaiFrameRate = 16;
	constexpr uint32_t g_syncHaltDspEsaiThreshold = 32;

	static_assert((g_syncEsaiFrameRate & (g_syncEsaiFrameRate - 1)) == 0, "esai frame sync rate must be power of two");
	static_assert(g_syncHaltDspEsaiThreshold >= g_syncEsaiFrameRate * 2, "esai DSP halt threshold must be greater than two times the sync rate");

	Hardware::Hardware()
		: m_rom(RomLoader::findROM())
		, m_uc(*this, m_rom)
		, m_dspA(*this, m_uc.getHdi08A(), 0)
		, m_dspB(*this, m_uc.getHdi08B(), 1)
		, m_samplerateInv(1.0 / g_samplerate)
		, m_semDspAtoB(2)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "No firmware found, expected firmware .bin with a size of " + std::to_string(Rom::MySize) + " bytes");

		m_dspA.getPeriph().getEsai().setCallback([this](dsp56k::Audio*){ onEsaiCallbackA(); }, 0);
		m_dspB.getPeriph().getEsai().setCallback([this](dsp56k::Audio*){ onEsaiCallbackB(); }, 0);

		m_ucThread.reset(new std::thread([this]
		{
			ucThreadFunc();
		}));

		while(!m_bootFinished)
			processAudio(8,8);
		m_midiOffsetCounter = 0;
	}

	Hardware::~Hardware()
	{
		m_destroy = true;

		while(m_destroy)
			processAudio(8,64);

		m_dspA.terminate();
		m_dspB.terminate();

		m_esaiFrameIndex = 0;
		m_maxEsaiCallbacks = std::numeric_limits<uint32_t>::max();
		m_esaiLatency = 0;

		while(!m_dspA.getDSPThread().runThread() || !m_dspB.getDSPThread().runThread())
		{
			// DSP A waits for space to push to DSP B
			m_semDspAtoB.notify();

			if(m_dspB.getPeriph().getEsai().getAudioInputs().full())
				m_dspB.getPeriph().getEsai().getAudioInputs().pop_front();

			// DSP B waits for ESAI rate limiting and for DSP A to provide audio data
			m_haltDSPcv.notify_all();
			if(m_dspA.getPeriph().getEsai().getAudioOutputs().empty())
				m_dspA.getPeriph().getEsai().getAudioOutputs().push_back({});
		}

		m_ucThread->join();
	}

	bool Hardware::isValid() const
	{
		return m_rom.isValid();
	}

	void Hardware::processUC()
	{
		syncUCtoDSP();

		const auto deltaCycles = m_uc.exec();

		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);
	}

	void Hardware::processAudio(uint32_t _frames, const uint32_t _latency)
	{
		getMidi().process(_frames);

		ensureBufferSize(_frames);

		dsp56k::TWord* outputs[12]{nullptr};
		outputs[1] = &m_audioOutputs[0].front();
		outputs[0] = &m_audioOutputs[1].front();
		outputs[3] = &m_audioOutputs[2].front();
		outputs[2] = &m_audioOutputs[3].front();
		outputs[4] = m_dummyOutput.data();
		outputs[5] = m_dummyOutput.data();
		outputs[6] = m_dummyOutput.data();
		outputs[7] = m_dummyOutput.data();
		outputs[8] = m_dummyOutput.data();
		outputs[9] = m_dummyOutput.data();
		outputs[10] = m_dummyOutput.data();
		outputs[11] = m_dummyOutput.data();

		auto& esaiB = m_dspB.getPeriph().getEsai();

//		LOG("B out " << esaiB.getAudioOutputs().size() << ", A out " << esaiA.getAudioOutputs().size() << ", B in " << esaiB.getAudioInputs().size());

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(64));
			_frames -= processCount;

			advanceSamples(processCount, _latency);

			const auto requiredSize = processCount > 8 ? processCount - 8 : 0;

			if(esaiB.getAudioOutputs().size() < requiredSize)
			{
				// reduce thread contention by waiting for output buffer to be full enough to let us grab the data without entering the read mutex too often

				std::unique_lock uLock(m_requestedFramesAvailableMutex);
				m_requestedFrames = requiredSize;
				m_requestedFramesAvailableCv.wait(uLock, [&]()
				{
					if(esaiB.getAudioOutputs().size() < requiredSize)
						return false;
					m_requestedFrames = 0;
					return true;
				});
			}

			// read output of DSP B to regular audio output
			esaiB.processAudioOutputInterleaved(outputs, processCount);

			outputs[0] += processCount;
			outputs[1] += processCount;
			outputs[2] += processCount;
			outputs[3] += processCount;
		}
	}
	
	void Hardware::processAudio(const synthLib::TAudioOutputs& _outputs, const uint32_t _frames, const uint32_t _latency)
	{
		processAudio(_frames, _latency);

		for(size_t i=0; i<_frames; ++i)
		{
			_outputs[0][i] = dsp56k::dsp2sample<float>(m_audioOutputs[0][i]);
			_outputs[1][i] = dsp56k::dsp2sample<float>(m_audioOutputs[1][i]);
			_outputs[2][i] = dsp56k::dsp2sample<float>(m_audioOutputs[2][i]);
			_outputs[3][i] = dsp56k::dsp2sample<float>(m_audioOutputs[3][i]);
		}
	}

	bool Hardware::sendMidi(const synthLib::SMidiEvent& _ev)
	{
		m_midiIn.push_back(_ev);
		return true;
	}

	void Hardware::notifyBootFinished()
	{
		m_bootFinished = true;
	}

	void Hardware::ensureBufferSize(const uint32_t _frames)
	{
		if(m_dummyInput.size() >= _frames)
			return;

		m_dummyInput.resize(_frames, 0);
		m_dummyOutput.resize(_frames, 0);

		for (auto& audioOutput : m_audioOutputs)
			audioOutput.resize(_frames, 0);

		m_dspAtoBBuffer.resize(_frames * 4);
	}

	void Hardware::onEsaiCallbackA()
	{
		// forward DSP A output to DSP B input
		const auto out = m_dspA.getPeriph().getEsai().getAudioOutputs().pop_front();

		dsp56k::Audio::RxFrame in;
		in.resize(out.size());

		in[0] = dsp56k::Audio::RxSlot{out[0][0]};
		in[1] = dsp56k::Audio::RxSlot{out[1][0]};
		in[2] = dsp56k::Audio::RxSlot{out[2][0]};
		in[3] = dsp56k::Audio::RxSlot{out[3][0]};

		m_dspB.getPeriph().getEsai().getAudioInputs().push_back(in);

		m_semDspAtoB.wait();
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

	void Hardware::onEsaiCallbackB()
	{
		m_semDspAtoB.notify();

		++m_esaiFrameIndex;

		processMidiInput();

		if((m_esaiFrameIndex & (g_syncEsaiFrameRate-1)) == 0)
			m_esaiFrameAddedCv.notify_one();

		m_requestedFramesAvailableMutex.lock();

		if(m_requestedFrames && m_dspB.getPeriph().getEsai().getAudioOutputs().size() >= m_requestedFrames)
		{
			m_requestedFramesAvailableMutex.unlock();
			m_requestedFramesAvailableCv.notify_one();
		}
		else
		{
			m_requestedFramesAvailableMutex.unlock();
		}

		if(m_esaiFrameIndex >= m_maxEsaiCallbacks + m_esaiLatency)
		{
			std::unique_lock uLock(m_haltDSPmutex);
			m_haltDSPcv.wait(uLock, [&]
			{
				return (m_maxEsaiCallbacks + m_esaiLatency) > m_esaiFrameIndex;
			});
		}
	}

	void Hardware::syncUCtoDSP()
	{
		if(m_remainingUcCycles > 0)
			return;

		// we can only use ESAI to clock the uc once it has been enabled
		if(m_esaiFrameIndex <= 0)
			return;

		if(m_esaiFrameIndex == m_lastEsaiFrameIndex)
		{
			resumeDSPs();
			std::unique_lock uLock(m_esaiFrameAddedMutex);
			m_esaiFrameAddedCv.wait(uLock, [this]{return m_esaiFrameIndex > m_lastEsaiFrameIndex;});
		}

		const auto esaiFrameIndex = m_esaiFrameIndex;
		const auto esaiDelta = esaiFrameIndex - m_lastEsaiFrameIndex;

		const auto ucClock = m_uc.getSim().getSystemClockHz();
		const double ucCyclesPerFrame = static_cast<double>(ucClock) * m_samplerateInv;

		// if the UC consumed more cycles than it was allowed to, remove them from remaining cycles
		m_remainingUcCyclesD += static_cast<double>(m_remainingUcCycles);

		// add cycles for the ESAI time that has passed
		m_remainingUcCyclesD += ucCyclesPerFrame * static_cast<double>(esaiDelta);

		// set new remaining cycle count
		m_remainingUcCycles = static_cast<int64_t>(m_remainingUcCyclesD);

		// and consume them
		m_remainingUcCyclesD -= static_cast<double>(m_remainingUcCycles);

		if(esaiDelta > g_syncHaltDspEsaiThreshold)
			haltDSPs();

		m_lastEsaiFrameIndex = esaiFrameIndex;
	}

	void Hardware::ucThreadFunc()
	{
		dsp56k::ThreadTools::setCurrentThreadName("MC68331");
		dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);

		while(!m_destroy)
		{
			processUC();
			processUC();
			processUC();
			processUC();
			processUC();
			processUC();
			processUC();
			processUC();
		}
		resumeDSPs();
		m_destroy = false;
	}

	void Hardware::advanceSamples(const uint32_t _samples, const uint32_t _latency)
	{
		{
			std::lock_guard uLockHalt(m_haltDSPmutex);
			m_maxEsaiCallbacks += _samples;
			m_esaiLatency = _latency;
		}
		m_haltDSPcv.notify_one();
	}

	void Hardware::haltDSPs()
	{
		if(m_dspHalted)
			return;
		m_dspHalted = true;
//		LOG("Halt");
		m_dspA.getHaltDSP().haltDSP();
		m_dspB.getHaltDSP().haltDSP();
	}

	void Hardware::resumeDSPs()
	{
		if(!m_dspHalted)
			return;
		m_dspHalted = false;
//		LOG("Resume");
		m_dspA.getHaltDSP().resumeDSP();
		m_dspB.getHaltDSP().resumeDSP();
	}

	bool Hardware::getButtonState(const ButtonType _type) const
	{
		return m_uc.getFrontPanel().getButtonState(_type);
	}

	void Hardware::setButtonState(const ButtonType _type, const bool _pressed)
	{
		m_uc.getFrontPanel().setButtonState(_type, _pressed);
	}

	uint8_t Hardware::getKnobPosition(KnobType _knob) const
	{
		return m_uc.getFrontPanel().getKnobPosition(_knob);
	}

	void Hardware::setKnobPosition(KnobType _knob, uint8_t _value)
	{
		return m_uc.getFrontPanel().setKnobPosition(_knob, _value);
	}
}
