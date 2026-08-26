#pragma once

#include "romfile.h"

#include "dsp56kEmu/dspthread.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

#include "synthLib/audioTypes.h"

namespace dsp56k
{
	class Jit;
}

namespace virusLib
{
	struct FrontpanelState;

	class DspSingle
	{
	public:
		DspSingle(uint32_t _memorySize, bool _use56367Peripherals = false, const char* _name = nullptr, bool _use56303Peripherals = false);
		virtual ~DspSingle();

		dsp56k::HDI08& getHDI08() { return m_hdi08; }
		dsp56k::Audio& getAudio() { return m_audio; }
		dsp56k::EsxiClock& getEsxiClock() { return m_esxiClock; }
		dsp56k::Peripherals56362& getPeriphX() { return m_periphX362; }
		dsp56k::Peripherals56367& getPeriphY() { return m_periphY367; }
		dsp56k::PeripheralsNop& getPeriphNop() { return m_periphNop; }
		dsp56k::DSP& getDSP() const { return *m_dsp; }
		dsp56k::Jit& getJIT() const { return *m_jit; }
		dsp56k::Memory& getMemory() const {return *m_memory; }

		void startDSPThread(bool _createDebugger);

		virtual void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency);
		virtual void processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, size_t _samples, uint32_t _latency);

		void disableESSI1();
		void drainESSI1();

		std::thread boot(const ROMFile::BootRom& _bootRom, const std::vector<dsp56k::TWord>& _commandStream);

		template<typename T> static void ensureSize(std::vector<T>& _buf, size_t _size)
		{
			if(_buf.size() >= _size)
				return;

			_buf.resize(_size, static_cast<T>(0));
		}

	protected:
		std::vector<uint32_t> m_dummyBufferInI;
		std::vector<uint32_t> m_dummyBufferOutI;
		std::vector<float> m_dummyBufferInF;
		std::vector<float> m_dummyBufferOutF;

	private:
		const std::string m_name;
		std::vector<uint8_t> m_buffer;

		dsp56k::DefaultMemoryValidator m_memoryValidator;
		dsp56k::Peripherals56367 m_periphY367;
		dsp56k::Peripherals56362 m_periphX362;
		dsp56k::Peripherals56303 m_periphX303;
		dsp56k::PeripheralsNop m_periphNop;

		dsp56k::HDI08& m_hdi08;
		dsp56k::Audio& m_audio;
		dsp56k::EsxiClock& m_esxiClock;

		dsp56k::Memory* m_memory = nullptr;
		dsp56k::DSP* m_dsp = nullptr;
		dsp56k::Jit* m_jit = nullptr;

		std::unique_ptr<dsp56k::DSPThread> m_dspThread;

		std::vector<dsp56k::TWord> m_commandStream;
		uint32_t m_commandStreamReadIndex = 0;
	};
}
