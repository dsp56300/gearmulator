#include "wDevice.h"

#include "dsp56kEmu/esaiclock.h"

namespace wLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params): synthLib::Device(_params)
	{
	}

	bool Device::setDspClockPercent(const uint32_t _percent)
	{
		auto* c = getDspEsxiClock();
		if(!c)
			return false;
		return c->setSpeedPercent(_percent);
	}

	uint32_t Device::getDspClockPercent() const
	{
		const auto* c = getDspEsxiClock();
		if(!c)
			return 0;
		return c->getSpeedPercent();
	}

	uint64_t Device::getDspClockHz() const
	{
		const auto* c = getDspEsxiClock();
		if(!c)
			return false;
		return c->getSpeedInHz();
	}

	void Device::process(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _size, const std::vector<synthLib::SMidiEvent>& _midiIn, std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		synthLib::Device::process(_inputs, _outputs, _size, _midiIn, _midiOut);
		m_numSamplesProcessed += static_cast<uint32_t>(_size);
	}
}
