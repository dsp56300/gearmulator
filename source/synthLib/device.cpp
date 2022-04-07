#include "device.h"

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/memory.h"

using namespace dsp56k;

namespace synthLib
{
	Device::Device(uint32_t _memorySize, uint32_t _externalMemStartAddress, bool _use56367Peripherals/* = false*/) : m_periphX(&m_periphY)
	{
		const size_t g_requiredMemSize	= alignedSize<DSP>() + alignedSize<Memory>() + _memorySize * MemArea_COUNT * sizeof(uint32_t);

		m_buffer.resize(alignedSize(g_requiredMemSize));

		auto* buf = &m_buffer[0];
		buf = alignedAddress(buf);

		auto* bufDSP = buf;
		auto* bufMem = bufDSP + alignedSize<DSP>();
		auto* bufBuf = bufMem + alignedSize<Memory>();

		m_periphX.getEsai().setCallback([this](Audio* _audio)
		{
			onAudioWritten();
		}, 0, 1);

		m_memory = new (bufMem)Memory(m_memoryValidator, _memorySize, reinterpret_cast<TWord*>(bufBuf));

		IPeripherals* periphY = &m_periphNop;
		if (_use56367Peripherals)
			periphY = &m_periphY;

		m_dsp = new (buf)DSP(*m_memory, &m_periphX, periphY);

		if(_externalMemStartAddress)
			m_memory->setExternalMemory(_externalMemStartAddress, true);
	}

	Device::~Device()
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

	void Device::dummyProcess(const uint32_t _numSamples)
	{
		std::vector<float> buf;
		buf.resize(_numSamples);
		const auto ptr = &buf[0];

		const float* in[2] = {ptr, ptr};
		float* out[2] = {ptr, ptr};

		std::vector<SMidiEvent> midi;

		process(in, out, _numSamples, midi, midi);
	}

	void Device::process(const float** _inputs, float** _outputs, const size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut)
	{
		for (const auto& ev : _midiIn)
			sendMidi(ev, _midiOut);

		m_periphX.getEsai().processAudioInterleaved(_inputs, _outputs, _size, 2, 2, m_extraLatency);
		readMidiOut(_midiOut);
	}

	void Device::setExtraLatencySamples(const uint32_t _size)
	{
		const uint32_t maxLatency = static_cast<uint32_t>(getPeriphX().getEsai().getAudioInputs()[0].capacity()) >> 1;

		m_extraLatency = std::min(_size, maxLatency);

		LOG("Latency set to " << m_extraLatency << " samples at " << getSamplerate() << " Hz");

		if(_size > maxLatency)
		{
			LOG("Warning, limited requested latency " << _size << " to maximum value " << maxLatency << ", audio will be out of sync!");
		}
	}

	void Device::startDSPThread()
	{
		m_dspThread.reset(new DSPThread(*m_dsp));
	}
}
