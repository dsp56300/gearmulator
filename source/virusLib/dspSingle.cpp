#include "dspSingle.h"

#include "dsp56kEmu/dsp.h"

#if DSP56300_DEBUGGER
#include "dsp56kDebugger/debugger.h"
#endif

namespace virusLib
{
	constexpr dsp56k::TWord g_externalMemStart	= 0x020000;

	DspSingle::DspSingle(uint32_t _memorySize, bool _use56367Peripherals/* = false*/, const char* _name/* = nullptr*/, bool _use56303Peripherals/* = false*/)
		: m_name(_name ? _name : std::string())
		, m_periphX362(_use56367Peripherals ? &m_periphY367 : nullptr)
		, m_hdi08(_use56303Peripherals ? m_periphX303.getHI08() : m_periphX362.getHDI08())
		, m_audio(_use56303Peripherals ? static_cast<dsp56k::Audio&>(m_periphX303.getEssi0()) : static_cast<dsp56k::Audio&>(m_periphX362.getEsai()))
		, m_esxiClock(_use56303Peripherals ? static_cast<dsp56k::EsxiClock&>(m_periphX303.getEssiClock()) : static_cast<dsp56k::EsxiClock&>(m_periphX362.getEsaiClock()))
	{
		const size_t requiredMemSize = 
			dsp56k::alignedSize<dsp56k::DSP>() + 
			dsp56k::alignedSize<dsp56k::Memory>() + 
			dsp56k::Memory::calcMemSize(_memorySize, g_externalMemStart) * sizeof(uint32_t);

		m_buffer.resize(dsp56k::alignedSize(requiredMemSize));

		auto* buf = m_buffer.data();
		buf = dsp56k::alignedAddress(buf);

		auto* bufDSPClass = buf;
		auto* bufMemClass = bufDSPClass + dsp56k::alignedSize<dsp56k::DSP>();
		auto* bufMemSpace = bufMemClass + dsp56k::alignedSize<dsp56k::Memory>();

		m_memory = new (bufMemClass)dsp56k::Memory(m_memoryValidator, _memorySize, _memorySize, g_externalMemStart, reinterpret_cast<dsp56k::TWord*>(bufMemSpace));

		dsp56k::IPeripherals* periphX = &m_periphX362;
		dsp56k::IPeripherals* periphY = &m_periphNop;

		if(_use56303Peripherals)
		{
			periphX = &m_periphX303;
			m_periphX303.getEssiClock().setExternalClockFrequency(4'000'000);	// 4 Mhz
			m_periphX303.getEssiClock().setSamplerate(12000000/256);
			drainESSI1();
		}

		if (_use56367Peripherals)
			periphY = &m_periphY367;

		m_dsp = new (buf)dsp56k::DSP(*m_memory, periphX, periphY);

		m_jit = &m_dsp->getJit();
	}

	DspSingle::~DspSingle()
	{
		m_dspThread.reset();

		if(m_dsp)
		{
			m_dsp->~DSP();
			m_memory->~Memory();

			m_dsp = nullptr;
			m_memory = nullptr;
		}
	}

	void DspSingle::startDSPThread(const bool _createDebugger)
	{
#if DSP56300_DEBUGGER
		const auto debugger = _createDebugger ? std::make_shared<dsp56kDebugger::Debugger>(*m_dsp) : std::shared_ptr<dsp56kDebugger::Debugger>();
#else
		const auto debugger = std::shared_ptr<dsp56k::DebuggerInterface>();
#endif

		m_dspThread.reset(new dsp56k::DSPThread(*m_dsp, m_name.empty() ? nullptr : m_name.c_str(), debugger));

#ifdef ZYNTHIAN
		m_dspThread->setLogToDebug(false);
		m_dspThread->setLogToStdout(false);
#endif
	}

	template<typename T> void processAudio(DspSingle& _dsp, const synthLib::TAudioInputsT<T>& _inputs, const synthLib::TAudioOutputsT<T>& _outputs, const size_t _samples, uint32_t _latency, std::vector<T>& _dummyIn, std::vector<T>& _dummyOut)
	{
		DspSingle::ensureSize(_dummyIn, _samples<<1);
		DspSingle::ensureSize(_dummyOut, _samples<<1);

		const T* dIn = _dummyIn.data();
		T* dOut = _dummyOut.data();

		const T* inputs[] = {_inputs[0] ? _inputs[0] : dIn, _inputs[1] ? _inputs[1] : dIn, dIn, dIn, dIn, dIn, dIn, dIn};
		T* outputs[] = 
			{ _outputs[0] ? _outputs[0] : dOut
			, _outputs[1] ? _outputs[1] : dOut
			, _outputs[2] ? _outputs[2] : dOut
			, _outputs[3] ? _outputs[3] : dOut
			, _outputs[4] ? _outputs[4] : dOut
			, _outputs[5] ? _outputs[5] : dOut
			, dOut, dOut, dOut, dOut, dOut, dOut};

		_dsp.getAudio().processAudioInterleaved(inputs, outputs, static_cast<uint32_t>(_samples), _latency);
	}
	void DspSingle::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples, const uint32_t _latency)
	{
		virusLib::processAudio(*this, _inputs, _outputs, _samples, _latency, m_dummyBufferInF, m_dummyBufferOutF);
	}

	void DspSingle::processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, const size_t _samples, const uint32_t _latency)
	{
		virusLib::processAudio(*this, _inputs, _outputs, _samples, _latency, m_dummyBufferInI, m_dummyBufferOutI);
	}

	void DspSingle::disableESSI1()
	{
		// Model A uses ESSI1 to send some initialization data to something.
		// Disable it once it has done doing that because otherwise it keeps running
		// and causes a performance hit that we can prevent by disabling it
		auto& essi = m_periphX303.getEssi1();
		auto crb = essi.readCRB();
		crb &= ~((1<<dsp56k::Essi::CRB_RE) | dsp56k::Essi::CRB_TE);
		essi.writeCRB(crb);
	}

	void DspSingle::drainESSI1()
	{
		auto& essi = m_periphX303.getEssi1();
		auto& ins = essi.getAudioInputs();
		auto& outs = essi.getAudioOutputs();

		while(!ins.full())
			ins.push_back({});

		while(!outs.empty())
			outs.pop_front();
	}

	std::thread DspSingle::boot(const ROMFile::BootRom& _bootRom, const std::vector<dsp56k::TWord>& _commandStream)
	{
		// Copy BootROM to DSP memory
		for (uint32_t i=0; i<_bootRom.data.size(); i++)
		{
			const auto p = _bootRom.offset + i;
			getMemory().set(dsp56k::MemArea_P, p, _bootRom.data[i]);
			getJIT().notifyProgramMemWrite(p);
		}

//		dsp.memory().saveAssembly((m_file + "_BootROM.asm").c_str(), bootRom.offset, bootRom.size, false, false, &periph);

		// Write command stream to HDI08 RX
		m_commandStream = _commandStream;
		m_commandStreamReadIndex = 0;

		while(!getHDI08().dataRXFull() && m_commandStreamReadIndex < m_commandStream.size())
			getHDI08().writeRX(&m_commandStream[m_commandStreamReadIndex++], 1);

		getHDI08().setReadRxCallback([this]
		{
			if(m_commandStreamReadIndex >= m_commandStream.size())
			{
				getHDI08().setReadRxCallback(nullptr);
				return;
			}

			getHDI08().writeRX(&m_commandStream[m_commandStreamReadIndex++], 1);
		});

		std::thread waitForCommandStreamWrite([this]()
		{
			while(m_commandStreamReadIndex < m_commandStream.size())
				std::this_thread::yield();
		});

		// Initialize the DSP
		getDSP().setPC(_bootRom.offset);
		return waitForCommandStreamWrite;
	}
}
