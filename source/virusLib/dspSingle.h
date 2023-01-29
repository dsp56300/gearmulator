#pragma once

#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/memory.h"

#include "../synthLib/audioTypes.h"

namespace dsp56k
{
	class Jit;
}

namespace virusLib
{
	class DspSingle
	{
	public:
		DspSingle(uint32_t _memorySize, bool _use56367Peripherals = false, const char* _name = nullptr);
		virtual ~DspSingle();

		dsp56k::HDI08& getHDI08() { return m_periphX.getHDI08(); }
		dsp56k::Peripherals56362& getPeriphX() { return m_periphX; }
		dsp56k::Peripherals56367& getPeriphY() { return m_periphY; }
		dsp56k::PeripheralsNop& getPeriphNop() { return m_periphNop; }
		dsp56k::DSP& getDSP() const { return *m_dsp; }
		dsp56k::Jit& getJIT() const { return *m_jit; }
		dsp56k::Memory& getMemory() const {return *m_memory; }

		void startDSPThread(bool _createDebugger);

		virtual void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency);
		virtual void processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency);

	private:
		const std::string m_name;
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
