#include "n2xhardware.h"

#include <algorithm>

#include "n2xromloader.h"
#include "dsp56kBase/audioworkgroup.h"
#include "dsp56kBase/threadtools.h"
#include "dsp56kEmu/utils.h"
#include "synthLib/deviceException.h"

namespace n2x
{
	namespace
	{
		// a word of a transmitted frame as a signed 24 bit value, zero if the frame has no such slot
		int32_t txWord(const dsp56k::Audio::TxFrame& _frame, const uint32_t _slot, const uint32_t _transmitter)
		{
			return _slot < _frame.size() ? dsp56k::signextend<int32_t, 24>(static_cast<int32_t>(_frame[_slot][_transmitter])) : 0;
		}
	}

	constexpr uint32_t g_syncEsaiFrameRate = 16;
	constexpr uint32_t g_syncHaltDspEsaiThreshold = 32;

	static_assert((g_syncEsaiFrameRate & (g_syncEsaiFrameRate - 1)) == 0, "esai frame sync rate must be power of two");
	static_assert(g_syncHaltDspEsaiThreshold >= g_syncEsaiFrameRate * 2, "esai DSP halt threshold must be greater than two times the sync rate");

	// time between the steps of a SHIFT combination, the firmware has to see each of them
	constexpr uint32_t g_shiftCombinationStepFrames = g_samplerate * 60 / 1000;

	Rom initRom(const std::vector<uint8_t>& _romData, const std::string& _romName)
	{
		if(_romData.empty())
			return RomLoader::findROM();
		Rom rom(_romData, _romName);
		if(rom.isValid())
			return rom;
		return RomLoader::findROM();
	}

	Hardware::Hardware(const std::vector<uint8_t>& _romData, const std::string& _romName)
		: m_rom(initRom(_romData, _romName))
		, m_uc(*this, m_rom)
		, m_dspA(*this, m_uc.getHdi08A(), 0)
		, m_dspB(*this, m_uc.getHdi08B(), 1)
		, m_samplerateInv(1.0 / g_samplerate)
	{
		if(!m_rom.isValid())
			throw synthLib::DeviceException(synthLib::DeviceError::FirmwareMissing, "No firmware found, expected firmware .bin with a size of " + std::to_string(Rom::MySize) + " bytes");

		initMixer();

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

		// a DSP that waits for the other one to catch up returns
		m_mixer.terminate();

		m_dspA.terminate();
		m_dspB.terminate();

		m_esaiFrameIndex = 0;
		m_esaiLatency = 0;

		while(!m_dspA.getDSPThread().runThread() || !m_dspB.getDSPThread().runThread())
		{
			// DSP B waits for ESAI rate limiting, a completed frame for room in the mixed output
			m_haltDSPSem.notify(999999);
			if(!m_mixedOutput.empty())
				m_mixedOutput.pop_front();
		}

		m_ucThread->join();
	}

	bool Hardware::isValid() const
	{
		return m_rom.isValid();
	}

	void Hardware::processUC()
	{
		if(m_remainingUcCycles <= 0)
			syncUCtoDSP();

		const auto deltaCycles = m_uc.exec();

		if(m_esaiFrameIndex > 0)
			m_remainingUcCycles -= static_cast<int64_t>(deltaCycles);
	}

	void Hardware::processAudio(uint32_t _frames, const uint32_t _latency)
	{
		m_midiIn.refill();

		getMidi().process(_frames);

		ensureBufferSize(_frames);

		uint32_t offset = 0;

		while (_frames)
		{
			const auto processCount = std::min(_frames, static_cast<uint32_t>(64));
			_frames -= processCount;

			processShiftCombination(processCount);
			advanceSamples(processCount, _latency);

			const auto requiredSize = processCount > 8 ? processCount - 8 : 0;

			if(m_mixedOutput.size() < requiredSize)
			{
				// reduce thread contention by waiting for output buffer to be full enough to let us grab the data without entering the read mutex too often

				std::unique_lock uLock(m_requestedFramesAvailableMutex);
				m_requestedFrames = requiredSize;
				m_requestedFramesAvailableCv.wait(uLock, [&]()
				{
					if(m_mixedOutput.size() < requiredSize)
						return false;
					m_requestedFrames = 0;
					return true;
				});
			}

			for(uint32_t i=0; i<processCount; ++i)
			{
				const auto frame = m_mixedOutput.pop_front();

				for(size_t c=0; c<frame.size(); ++c)
					m_audioOutputs[c][offset + i] = frame[c];
			}

			offset += processCount;
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
		if(m_audioOutputs[0].size() >= _frames)
			return;

		for (auto& audioOutput : m_audioOutputs)
			audioOutput.resize(_frames, 0);
	}

	void Hardware::initMixer()
	{
		// both lanes before the first frame, see SharedAudioReducer::addProducer
		m_mixerLaneA = m_mixer.addProducer();
		m_mixerLaneB = m_mixer.addProducer();

		m_mixer.setCompletionCallback([this](uint64_t, const MixFrame& _frame)
		{
			onMixedFrame(_frame);
		});

		// DSP B outputs a frame of DSP A four frames later: two frames wait in its ESAI input, two in its firmware
		for(uint32_t i=0; i<4; ++i)
			m_mixer.addFrame(m_mixerLaneA, {});

		m_dspA.getPeriph().getEsai().setWriteTxCallback([this](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _frame)
		{
			++_frameIndex;

			// DSP B puts the slots 2, 1, 0 and 3 of DSP A on its outputs 0 to 3
			m_mixer.addFrame(m_mixerLaneA, {txWord(_frame, 2, 0), txWord(_frame, 1, 0), txWord(_frame, 0, 0), txWord(_frame, 3, 0)});
		});

		m_dspB.getPeriph().getEsai().setReadRxCallback([](uint64_t& _frameIndex, dsp56k::Audio::RxFrame& _frame)
		{
			++_frameIndex;

			_frame.resize(4);
			for(uint32_t i=0; i<4; ++i)
				_frame[i].fill(0);
		});

		m_dspB.getPeriph().getEsai().setWriteTxCallback([this](uint64_t& _frameIndex, const dsp56k::Audio::TxFrame& _frame)
		{
			++_frameIndex;

			// slot 0 is the left channel: FST of DSP B is the LRCK of both DACs, which take the left sample while it is high
			m_mixer.addFrame(m_mixerLaneB, {txWord(_frame, 0, 0), txWord(_frame, 1, 0), txWord(_frame, 0, 1), txWord(_frame, 1, 1)});

			onEsaiCallbackB();
		});
	}

	void Hardware::onMixedFrame(const MixFrame& _frame)
	{
		// DSP B saturates the sum
		std::array<dsp56k::TWord, 4> out;

		for(size_t i=0; i<out.size(); ++i)
			out[i] = static_cast<dsp56k::TWord>(std::clamp(_frame[i], -0x800000, 0x7fffff)) & 0xffffff;

		m_mixedOutput.push_back(out);

		m_requestedFramesAvailableMutex.lock();

		if(m_requestedFrames && m_mixedOutput.size() >= m_requestedFrames)
		{
			m_requestedFramesAvailableMutex.unlock();
			m_requestedFramesAvailableCv.notify_one();
		}
		else
		{
			m_requestedFramesAvailableMutex.unlock();
		}
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
		++m_esaiFrameIndex;

		processMidiInput();

		if((m_esaiFrameIndex & (g_syncEsaiFrameRate-1)) == 0)
			m_esaiFrameAddedCv.notify_one();

		m_haltDSPSem.wait(1);
	}

	void Hardware::syncUCtoDSP()
	{
		assert(m_remainingUcCycles <= 0);

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

		dsp56k::AudioWorkgroup::Member workgroup;

		while(!m_destroy)
		{
			workgroup.update();
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
		// if the latency was higher first but now is lower, we might report < 0 samples. In this case we
		// cannot notify but have to wait for another sample block until we can notify again

		const auto latencyDiff = static_cast<int>(_latency) - static_cast<int>(m_esaiLatency);
		m_esaiLatency = _latency;

		const auto notifyCount = static_cast<int>(_samples) + latencyDiff + m_dspNotifyCorrection;

		if (notifyCount > 0)
		{
			m_haltDSPSem.notify(notifyCount);
			m_dspNotifyCorrection = 0;
		}
		else
		{
			m_dspNotifyCorrection = notifyCount;
		}
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

	void Hardware::pressShiftCombination(const ButtonType _button)
	{
		if(m_shiftCombinationStep)
			return;

		setButtonState(ButtonType::Shift, true);

		m_shiftCombinationButton = _button;
		m_shiftCombinationStep = 1;
		m_shiftCombinationWait = g_shiftCombinationStepFrames;
	}

	void Hardware::processShiftCombination(const uint32_t _frames)
	{
		if(!m_shiftCombinationStep)
			return;

		if(m_shiftCombinationWait > _frames)
		{
			m_shiftCombinationWait -= _frames;
			return;
		}

		m_shiftCombinationWait = g_shiftCombinationStepFrames;

		switch(m_shiftCombinationStep++)
		{
		case 1:
			setButtonState(m_shiftCombinationButton, true);
			break;
		case 2:
			setButtonState(m_shiftCombinationButton, false);
			break;
		default:
			setButtonState(ButtonType::Shift, false);
			m_shiftCombinationStep = 0;
			break;
		}
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
