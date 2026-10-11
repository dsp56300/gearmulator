#include "consoleTools.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "device.h"

namespace synthLib
{
	namespace
	{
		// a float sample as the 24 bit two's complement word the DSPs output
		uint32_t toWord(const float _sample)
		{
			const auto v = std::clamp(_sample * 8388608.0f, -8388608.0f, 8388607.0f);
			return static_cast<uint32_t>(static_cast<int32_t>(std::floor(v))) & 0xffffff;
		}
	}

	StereoWavSink::StereoWavSink(const std::string& _filename, const uint32_t _samplerate) : m_writer(_filename, _samplerate, false)
	{
	}

	void StereoWavSink::write(const uint32_t* _left, const uint32_t* _right, const size_t _frames)
	{
		m_writer.append([&](std::vector<uint32_t>& _dst)
		{
			for (size_t i=0; i<_frames; ++i)
			{
				_dst.push_back(_left[i]);
				_dst.push_back(_right[i]);
			}
		});
	}

	void StereoWavSink::write(const float* _left, const float* _right, const size_t _frames)
	{
		m_writer.append([&](std::vector<uint32_t>& _dst)
		{
			for (size_t i=0; i<_frames; ++i)
			{
				_dst.push_back(toWord(_left[i]));
				_dst.push_back(toWord(_right[i]));
			}
		});
	}

	RealtimeMeter::RealtimeMeter(const uint32_t _samplerate) : m_samplerate(_samplerate), m_start(Clock::now()), m_intervalStart(m_start)
	{
	}

	void RealtimeMeter::add(const uint32_t _frames)
	{
		m_totalFrames += _frames;
		m_intervalFrames += _frames;

		if (m_intervalFrames < m_samplerate)
			return;

		const auto now = Clock::now();
		const auto interval = std::chrono::duration<double>(now - m_intervalStart).count();
		const auto total = std::chrono::duration<double>(now - m_start).count();
		m_intervalStart = now;

		const auto percent = [this](const uint64_t _frames, const double _seconds)
		{
			return static_cast<int>(100.0 * static_cast<double>(_frames) / (_seconds * m_samplerate));
		};

		std::cout << "Recorded " << (m_totalFrames / m_samplerate) << " sec"
			<< ", last interval speed " << percent(m_intervalFrames, interval) << "%, "
			<< "total average " << percent(m_totalFrames, total) << "%\n";

		m_intervalFrames -= m_samplerate;
	}

	DeviceRunner::DeviceRunner(Device& _device, const uint32_t _blockSize) : m_device(_device), m_blockSize(_blockSize)
	{
		const auto ins = std::min<size_t>(_device.getChannelCountIn(), m_inputs.size());
		const auto outs = std::min<size_t>(_device.getChannelCountOut(), m_outputs.size());

		m_buffers.resize(ins + outs, std::vector<float>(_blockSize, 0.0f));

		for (size_t i=0; i<ins; ++i)
			m_inputs[i] = m_buffers[i].data();
		for (size_t i=0; i<outs; ++i)
			m_outputs[i] = m_buffers[ins + i].data();
	}

	void DeviceRunner::process()
	{
		m_device.process(m_inputs, m_outputs, m_blockSize, m_midiIn, m_midiOut);
		m_midiIn.clear();

		for (const auto& e : m_midiOut)
			m_frontPanel.receive(e.sysex);

		if (m_wavSink && m_outputs[0] && m_outputs[1])
			m_wavSink->write(m_outputs[0], m_outputs[1], m_blockSize);

		m_framesProcessed += m_blockSize;
	}
}
