#pragma once

#include <vector>

#include "device.h"

namespace synthLib
{
	// A device whose clock is the one of its DSPs: setting it sets all of them, the first one is reported. A template so
	// that synthLib does not depend on the DSP emulator, TClock is dsp56k::EsxiClock
	template<typename TClock>
	class DspClockDevice : public Device
	{
	public:
		using Device::Device;

		bool setDspClockPercent(const uint32_t _percent) override
		{
			const auto clocks = getClocks();
			bool res = !clocks.empty();
			for (auto* clock : clocks)
				res &= clock->setSpeedPercent(_percent);
			return res;
		}

		uint32_t getDspClockPercent() const override
		{
			const auto clocks = getClocks();
			return clocks.empty() ? 100 : clocks.front()->getSpeedPercent();
		}

		uint64_t getDspClockHz() const override
		{
			const auto clocks = getClocks();
			return clocks.empty() ? 0 : clocks.front()->getSpeedInHz();
		}

	protected:
		// the clocks of all DSPs of the device, none while it has none
		virtual void getDspClocks(std::vector<TClock*>& _clocks) const = 0;

	private:
		std::vector<TClock*> getClocks() const
		{
			std::vector<TClock*> clocks;
			getDspClocks(clocks);
			return clocks;
		}
	};
}
