#include "xt.h"

#include "synthLib/midiTypes.h"

#include "dsp56kEmu/threadtools.h"

#include "xtHardware.h"
#include "xtRomLoader.h"

namespace xt
{
	Xt::Xt(const std::vector<uint8_t>& _romData, const std::string& _romName)
	{
		m_hw.reset(new Hardware(_romData, _romName));

		if(!isValid())
			return;

		m_midiOutBuffer.reserve(1024);

		m_ucThread.reset(new std::thread([&]()
		{
			dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);
			dsp56k::ThreadTools::setCurrentThreadName("MC68331");
			while(!m_destroy)
				processUcThread();
			m_destroy = false;
			m_hw->ucThreadTerminated();
		}));

		m_hw->initVoiceExpansion();

		m_hw->getUC().setLcdDirtyCallback([this]
		{
			m_dirtyFlags.fetch_or(static_cast<uint32_t>(DirtyFlags::Lcd));
		});
		m_hw->getUC().setLedsDirtyCallback([this]
		{
			m_dirtyFlags.fetch_or(static_cast<uint32_t>(DirtyFlags::Leds));
		});
	}

	Xt::~Xt()
	{
		if(!isValid())
			return;

		// we need to have passed the boot stage
		m_hw->processAudio(1);

		m_destroy = true;

		// DSP needs to run to let the uc thread wake up
		const auto& esai = m_hw->getDSP().getPeriph().getEssi0();
		while(m_destroy)
		{
			if(!esai.getAudioOutputs().empty())
				m_hw->processAudio(1);
			else
				std::this_thread::yield();
		}

		m_ucThread->join();
		m_ucThread.reset();
		m_hw.reset();
	}

	bool Xt::isValid() const
	{
		return m_hw && m_hw->isValid();
	}

	void Xt::process(const float** _inputs, float** _outputs, uint32_t _frames, uint32_t _latency)
	{
		std::lock_guard lock(m_mutex);

		m_hw->ensureBufferSize(_frames);

		// convert inputs from float to DSP words
		auto& dspIns = m_hw->getAudioInputs();

		for(size_t c=0; c<dspIns.size(); ++c)
		{
			for(uint32_t i=0; i<_frames; ++i)
				dspIns[c][i] = dsp56k::sample2dsp(_inputs[c][i]);
		}

		internalProcess(_frames, _latency);

		// convert outputs from DSP words to float
		const auto& dspOuts = m_hw->getAudioOutputs();

		for(size_t c=0; c<dspOuts.size(); ++c)
		{
			for(uint32_t i=0; i<_frames; ++i)
				_outputs[c][i] = dsp56k::dsp2sample<float>(dspOuts[c][i]);
		}
	}

	void Xt::process(uint32_t _frames, uint32_t _latency)
	{
		std::lock_guard lock(m_mutex);
		internalProcess(_frames, _latency);
	}

	void Xt::internalProcess(uint32_t _frames, uint32_t _latency)
	{
		// process audio
		m_hw->processAudio(_frames, _latency);

		// receive midi output
		m_hw->receiveMidi(m_midiOutBuffer);
	}

	TAudioInputs& Xt::getAudioInputs() const
	{
		return m_hw->getAudioInputs();
	}

	TAudioOutputs& Xt::getAudioOutputs() const
	{
		return m_hw->getAudioOutputs();
	}

	void Xt::sendMidiEvent(const synthLib::SMidiEvent& _ev) const
	{
		m_hw->sendMidi(_ev);
	}

	void Xt::receiveMidi(std::vector<uint8_t>& _buffer)
	{
		std::lock_guard lock(m_mutex);
		std::swap(_buffer, m_midiOutBuffer);
		m_midiOutBuffer.clear();
	}

	Hardware* Xt::getHardware() const
	{
		return m_hw.get();
	}

	bool Xt::isBootCompleted() const
	{
		return m_hw && m_hw->isBootCompleted();
	}

	Xt::DirtyFlags Xt::getDirtyFlags()
	{
		const auto r = m_dirtyFlags.exchange(0);
		return static_cast<DirtyFlags>(r);
	}

	void Xt::readLCD(std::array<char, 80>& _lcdData) const
	{
		_lcdData = m_hw->getUC().getLcd().getData();
	}

	bool Xt::getLedState(LedType _led) const
	{
		return m_hw->getUC().getLedState(_led);
	}

	bool Xt::getButton(ButtonType _button) const
	{
		return m_hw->getUC().getButton(_button);
	}

	void Xt::processUcThread() const
	{
		for(size_t i=0; i<32; ++i)
		{
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
			m_hw->process();
		}
	}
}
