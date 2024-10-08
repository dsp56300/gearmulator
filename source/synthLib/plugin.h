#pragma once

#include <mutex>

#include "midiTypes.h"
#include "resamplerInOut.h"
#include "buildconfig.h"

#include "dsp56kEmu/ringbuffer.h"

#include "deviceTypes.h"
#include "midiClock.h"

namespace synthLib
{
	class Device;

	class Plugin
	{
	public:
		Plugin(Device* _device);

		void addMidiEvent(const SMidiEvent& _ev);

		bool setPreferredDeviceSamplerate(float _samplerate);

		void setHostSamplerate(float _hostSamplerate, float _preferredDeviceSamplerate);
		float getHostSamplerate() const { return m_hostSamplerate; }
		float getHostSamplerateInv() const { return m_hostSamplerateInv; }

		void setBlockSize(uint32_t _blockSize);

		uint32_t getLatencyMidiToOutput() const;
		uint32_t getLatencyInputToOutput() const;

		void process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _count, float _bpm, float _ppqPos, bool _isPlaying);
		void getMidiOut(std::vector<SMidiEvent>& _midiOut);

		bool isValid() const;

		void setDevice(Device* _device);

#if !SYNTHLIB_DEMO_MODE
		bool getState(std::vector<uint8_t>& _state, StateType _type) const;
		bool setState(const std::vector<uint8_t>& _state);
#endif
		void insertMidiEvent(const SMidiEvent& _ev);

		bool setLatencyBlocks(uint32_t _latencyBlocks);
		uint32_t getLatencyBlocks() const { return m_extraLatencyBlocks; }

	private:
		void processMidiClock(float _bpm, float _ppqPos, bool _isPlaying, size_t _sampleCount);
		float* getDummyBuffer(size_t _minimumSize);
		void updateDeviceLatency();
		void processMidiInEvents();
		void processMidiInEvent(const SMidiEvent& _ev);

		dsp56k::RingBuffer<SMidiEvent, 1024, false> m_midiInRingBuffer;
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;

		SMidiEvent m_pendingSysexInput;

		ResamplerInOut m_resampler;
		mutable std::mutex m_lock;
		mutable std::mutex m_lockAddMidiEvent;

		Device* m_device;

		std::vector<float> m_dummyBuffer;

		float m_hostSamplerate = 0.0f;
		float m_hostSamplerateInv = 0.0f;

		uint32_t m_blockSize = 0;

		uint32_t m_deviceLatencyMidiToOutput = 0;
		uint32_t m_deviceLatencyInputToOutput = 0;

		MidiClock m_midiClock;

		uint32_t m_extraLatencyBlocks = 1;

		float m_deviceSamplerate = 0.0f;
	};
}
