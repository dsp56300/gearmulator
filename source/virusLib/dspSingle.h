#pragma once

#include "dsp56kEmu/dspthread.h"

#include "../synthLib/audioTypes.h"

namespace virusLib
{
	class DspSingle
	{
	public:
		DspSingle(uint32_t _memorySize, uint32_t _externalMemStartAddress, bool _use56367Peripherals = false);
		~DspSingle();

		dsp56k::HDI08& getHDI08() { return m_periphX.getHDI08(); }
		dsp56k::Peripherals56362& getPeriphX() { return m_periphX; }
		dsp56k::Peripherals56367& getPeriphY() { return m_periphY; }
		dsp56k::DSP& getDSP() const { return *m_dsp; }
		dsp56k::Jit& getJIT() const { return *m_jit; }

		void startDSPThread();

		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency);

	private:
		std::vector<uint8_t> m_buffer;

		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Peripherals56367 m_periphY;
		dsp56k::Peripherals56362 m_periphX;
		dsp56k::PeripheralsNop m_periphNop;

		dsp56k::Memory* m_memory = nullptr;
		dsp56k::DSP* m_dsp = nullptr;
		dsp56k::Jit* m_jit = nullptr;

		std::unique_ptr<dsp56k::DSPThread> m_dspThread;
	};
}
