#include "wDevice.h"

#include "dsp56kEmu/esaiclock.h"

namespace wLib
{
	Device::Device(const synthLib::DeviceCreateParams& _params): synthLib::Device(_params), m_midiOutParser(synthLib::MidiEventSource::Device)
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
}
