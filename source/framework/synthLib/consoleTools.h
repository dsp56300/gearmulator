#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "audioTypes.h"
#include "frontPanelSysex.h"
#include "midiTypes.h"
#include "wavWriter.h"

namespace synthLib
{
	class Device;

	// The stereo output of a test console as a WAV file, from the DSP's 24 bit words or from floats
	class StereoWavSink
	{
	public:
		StereoWavSink(const std::string& _filename, uint32_t _samplerate);

		void write(const uint32_t* _left, const uint32_t* _right, size_t _frames);
		void write(const float* _left, const float* _right, size_t _frames);

	private:
		AsyncWriter m_writer;
	};

	// Prints once per second of audio how fast it was rendered compared to real time, for the last second and overall
	class RealtimeMeter
	{
	public:
		explicit RealtimeMeter(uint32_t _samplerate);

		void add(uint32_t _frames);

	private:
		using Clock = std::chrono::steady_clock;

		const uint32_t m_samplerate;
		const Clock::time_point m_start;
		Clock::time_point m_intervalStart;
		uint64_t m_totalFrames = 0;
		uint32_t m_intervalFrames = 0;
	};

	// Runs a device block by block for a test console: MIDI given to addMidi goes in with the next block, the front
	// panel frames the device sends are decoded by getFrontPanel(), the first two outputs go to the WAV sink, if any
	class DeviceRunner
	{
	public:
		DeviceRunner(Device& _device, uint32_t _blockSize);

		FrontPanelTransport& getFrontPanel() { return m_frontPanel; }
		void addMidi(const SMidiEvent& _ev) { m_midiIn.push_back(_ev); }
		void setWavSink(StereoWavSink* _sink) { m_wavSink = _sink; }

		void process();

		uint64_t getFramesProcessed() const { return m_framesProcessed; }

	private:
		Device& m_device;
		const uint32_t m_blockSize;

		std::vector<std::vector<float>> m_buffers;	// inputs, silent, then outputs
		TAudioInputs m_inputs{};
		TAudioOutputs m_outputs{};

		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;

		FrontPanelTransport m_frontPanel;
		StereoWavSink* m_wavSink = nullptr;
		uint64_t m_framesProcessed = 0;
	};
}
