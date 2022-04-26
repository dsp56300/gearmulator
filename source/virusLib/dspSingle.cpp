#include "dspSingle.h"

namespace virusLib
{
	constexpr dsp56k::TWord g_externalMemStart	= 0x020000;

	DspSingle::DspSingle(uint32_t _memorySize, bool _use56367Peripherals/* = false*/, const char* _name/* = nullptr*/) : m_name(_name ? _name : std::string()), m_periphX(&m_periphY)
	{
		const size_t g_requiredMemSize	= dsp56k::alignedSize<dsp56k::DSP>() + dsp56k::alignedSize<dsp56k::Memory>() + _memorySize * dsp56k::MemArea_COUNT * sizeof(uint32_t);

		m_buffer.resize(dsp56k::alignedSize(g_requiredMemSize));

		auto* buf = &m_buffer[0];
		buf = dsp56k::alignedAddress(buf);

		auto* bufDSPClass = buf;
		auto* bufMemClass = bufDSPClass + dsp56k::alignedSize<dsp56k::DSP>();
		auto* bufMemSpace = bufMemClass + dsp56k::alignedSize<dsp56k::Memory>();

		m_memory = new (bufMemClass)dsp56k::Memory(m_memoryValidator, _memorySize, reinterpret_cast<dsp56k::TWord*>(bufMemSpace));

		dsp56k::IPeripherals* periphY = &m_periphNop;

		if (_use56367Peripherals)
			periphY = &m_periphY;

		m_dsp = new (buf)dsp56k::DSP(*m_memory, &m_periphX, periphY);
		m_jit = &m_dsp->getJit();

		m_memory->setExternalMemory(g_externalMemStart, true);
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

	void DspSingle::startDSPThread()
	{
		m_dspThread.reset(new dsp56k::DSPThread(*m_dsp, m_name.empty() ? nullptr : m_name.c_str()));
	}

	void DspSingle::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples, uint32_t _latency)
	{
		const float* inputs[] = {_inputs[0], _inputs[1], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
		float* outputs[] = {_outputs[0], _outputs[1], _outputs[2], _outputs[3], _outputs[4], _outputs[5], nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};

		getPeriphX().getEsai().processAudioInterleaved(inputs, outputs, static_cast<uint32_t>(_samples), _latency);
	}
}
