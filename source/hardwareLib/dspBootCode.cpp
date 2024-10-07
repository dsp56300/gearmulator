#include "dspBootCode.h"

#include "dsp56kEmu/dsp.h"

namespace hwLib
{
	DspBoot::DspBoot(dsp56k::DSP& _dsp) : m_dsp(_dsp)
	{
		// H(D)I08 interface needs to be enabled after boot, set it up now already
		auto enableHdi08 = [](dsp56k::HDI08& _hdi08)
		{
			_hdi08.writePortControlRegister(1<<dsp56k::HDI08::HPCR_HEN);
		};

		if(auto* periph56362 = dynamic_cast<dsp56k::Peripherals56362*>(m_dsp.getPeriph(0)))
			enableHdi08(periph56362->getHDI08());
		else if(auto* periph56303 = dynamic_cast<dsp56k::Peripherals56303*>(m_dsp.getPeriph(0)))
			enableHdi08(periph56303->getHI08());
	}

	bool DspBoot::hdiWriteTX(const dsp56k::TWord& _val)
	{
		// DSP Boot Code emulation. The following bytes are sent to the DSP via HDI08:
		//
		// * Length = number of words to be received
		// * Address = target address where words will be written to
		// * 'Length' number of words
		//
		// After completion, DSP jumps to 'Address' to execute the code it just received

		switch (m_state)
		{
		case State::Length:
			m_remaining = _val;
			m_length = _val;
			m_state = State::Address;
			return false;
		case State::Address:
			m_address = _val;
			m_initialPC = _val;

			LOG("DSP Boot: " << m_remaining << " words, initial PC " << HEX(m_address));

			// r0 is used as counter, r1 holds the initial PC
			m_dsp.regs().r[0].var = static_cast<int32_t>(m_address + m_remaining);
			m_dsp.regs().r[1].var = static_cast<int32_t>(m_address);

			m_dsp.regs().sr.var &= ~0xff;	// ccr is cleared before jump to r1
			m_dsp.setPC(m_address);			// jmp (r1)

			m_state = State::Data;
			return false;
		case State::Data:
			m_dsp.memory().set(dsp56k::MemArea_P, m_address, _val);
			m_dsp.getJit().notifyProgramMemWrite(m_address);
			++m_address;
			if(0 == --m_remaining)
			{
				LOG("DSP Boot: finished");
				m_state = State::Finished;
				return true;
			}
			return false;
		case State::Finished:
			return true;
		}
		assert(false && "invalid state");
		return false;
	}
}
