#pragma once

#include "synthLib/device.h"
#include "synthLib/midiBufferParser.h"

namespace dsp56k
{
	class EsxiClock;
}

namespace wLib
{
	class Device : public synthLib::Device
	{
	public:
		explicit Device(const synthLib::DeviceCreateParams& _params);
		bool setDspClockPercent(uint32_t _percent) override;
		uint32_t getDspClockPercent() const override;
		uint64_t getDspClockHz() const override;

	protected:
		virtual dsp56k::EsxiClock* getDspEsxiClock() const = 0;

		std::vector<uint8_t>				m_midiOutBuffer;
		synthLib::MidiBufferParser			m_midiOutParser;
		std::vector<synthLib::SMidiEvent>	m_customSysexOut;
	};
}
