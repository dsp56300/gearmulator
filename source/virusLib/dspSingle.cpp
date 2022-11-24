#include "dspSingle.h"

#if DSP56300_DEBUGGER
#include "dsp56kDebugger/debugger.h"
#endif

namespace virusLib
{
	constexpr dsp56k::TWord g_externalMemStart	= 0x020000;

	DspSingle::DspSingle(uint32_t _memorySize, bool _use56367Peripherals/* = false*/, const char* _name/* = nullptr*/) : m_name(_name ? _name : std::string()), m_periphX(&m_periphY)
	{
		const size_t requiredMemSize = 
			dsp56k::alignedSize<dsp56k::DSP>() + 
			dsp56k::alignedSize<dsp56k::Memory>() + 
			dsp56k::Memory::calcMemSize(_memorySize, g_externalMemStart) * sizeof(uint32_t);

		m_buffer.resize(dsp56k::alignedSize(requiredMemSize));

		auto* buf = &m_buffer[0];
		buf = dsp56k::alignedAddress(buf);

		auto* bufDSPClass = buf;
		auto* bufMemClass = bufDSPClass + dsp56k::alignedSize<dsp56k::DSP>();
		auto* bufMemSpace = bufMemClass + dsp56k::alignedSize<dsp56k::Memory>();

		m_memory = new (bufMemClass)dsp56k::Memory(m_memoryValidator, _memorySize, _memorySize, g_externalMemStart, reinterpret_cast<dsp56k::TWord*>(bufMemSpace));

		dsp56k::IPeripherals* periphY = &m_periphNop;

		if (_use56367Peripherals)
			periphY = &m_periphY;

		m_dsp = new (buf)dsp56k::DSP(*m_memory, &m_periphX, periphY);
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
	}

	template<typename T> void processAudio(DspSingle& _dsp, const synthLib::TAudioInputsT<T>& _inputs, const synthLib::TAudioOutputsT<T>& _outputs, const size_t _samples, uint32_t _latency)
	{
		const T* inputs[] = {_inputs[1], _inputs[0], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		T* outputs[] = {_outputs[1], _outputs[0], _outputs[3], _outputs[2], _outputs[5], _outputs[4], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		_dsp.getPeriphX().getEsai().processAudioInterleaved(inputs, outputs, static_cast<uint32_t>(_samples), _latency);
	}
	void DspSingle::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples, uint32_t _latency)
	{
		virusLib::processAudio(*this, _inputs, _outputs, _samples, _latency);
	}

	void DspSingle::processAudio(const synthLib::TAudioInputsInt& _inputs, const synthLib::TAudioOutputsInt& _outputs, const size_t _samples, uint32_t _latency)
	{
		virusLib::processAudio(*this, _inputs, _outputs, _samples, _latency);
	}
}
