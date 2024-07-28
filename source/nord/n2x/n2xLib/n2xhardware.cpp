#include "n2xhardware.h"

namespace n2x
{
	constexpr uint32_t g_syncEsaiFrameRate = 16;
	constexpr uint32_t g_syncHaltDspEsaiThreshold = 32;

	static_assert((g_syncEsaiFrameRate & (g_syncEsaiFrameRate - 1)) == 0, "esai frame sync rate must be power of two");
	static_assert(g_syncHaltDspEsaiThreshold >= g_syncEsaiFrameRate * 2, "esai DSP halt threshold must be greater than two times the sync rate");

	Hardware::Hardware()
		: m_uc(m_rom)
		, m_dspA(*this, m_uc.getHdi08A(), 0)
		, m_dspB(*this, m_uc.getHdi08B(), 1)
		, m_samplerateInv(1.0 / g_samplerate)
		, m_semDspAtoB(1)
	{
		if(!m_rom.isValid())
			return;

		m_dspA.getPeriph().getEsai().setCallback([this](dsp56k::Audio*){ onEsaiCallbackA(); }, 0);
		m_dspB.getPeriph().getEsai().setCallback([this](dsp56k::Audio*){ onEsaiCallbackB(); }, 0);
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
		ensureBufferSize(_frames);

		dsp56k::TWord* outputs[12]{nullptr};
		outputs[0] = &m_audioOutputs[0].front();
		outputs[1] = &m_audioOutputs[1].front();
		outputs[2] = &m_audioOutputs[2].front();
		outputs[3] = &m_audioOutputs[3].front();
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

	void Hardware::onEsaiCallbackB()
	{
		m_semDspAtoB.notify();

		++m_esaiFrameIndex;

//		processMidiInput();

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

	void Hardware::haltDSPs()
	{
		if(m_dspHalted)
			return;
		m_dspHalted = true;
		m_dspB.getHaltDSP().haltDSP();
	}

	void Hardware::resumeDSPs()
	{
		if(!m_dspHalted)
			return;
		m_dspHalted = false;
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
}
