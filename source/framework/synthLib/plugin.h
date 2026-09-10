#pragma once

#include <atomic>
#include <mutex>
#include <functional>

#include "midiTypes.h"
#include "resamplerInOut.h"
#include "buildconfig.h"

#include "dsp56kBase/ringbuffer.h"

#include "deviceTypes.h"
#include "midiClock.h"

namespace synthLib
{
	class Device;

	class Plugin
	{
	public:
		using CallbackDeviceInvalid = std::function<Device*(Device*)>;

		Plugin(Device* _device, CallbackDeviceInvalid _callbackDeviceInvalid);

		void addMidiEvent(const SMidiEvent& _ev);

		bool setPreferredDeviceSamplerate(float _samplerate);

		void setHostSamplerate(float _hostSamplerate, float _preferredDeviceSamplerate);
		void setResamplerMode(Resampler::Mode _mode);
		float getHostSamplerate() const { return m_hostSamplerate; }
		float getHostSamplerateInv() const { return m_hostSamplerateInv; }

		void setBlockSize(uint32_t _blockSize);

		uint32_t getLatencyMidiToOutput() const;
		uint32_t getLatencyInputToOutput() const;

		void process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _count, float _bpm, float _ppqPos,
			bool _isPlaying, bool _hasPpqPosition);
		void getMidiOut(std::vector<SMidiEvent>& _midiOut);

		bool isValid() const;

		void setDevice(Device* _device);

#if !SYNTHLIB_DEMO_MODE
		bool getState(std::vector<uint8_t>& _state, StateType _type) const;
		bool setState(const std::vector<uint8_t>& _state) const;
#endif
		void insertMidiEvent(const SMidiEvent& _ev);

		bool setLatencyBlocks(uint32_t _latencyBlocks);
		uint32_t getLatencyBlocks() const { return m_extraLatencyBlocks; }

		void setMidiClockEnabled(bool _enabled);

		// A device whose firmware retunes its clock is noticed on the audio thread, but
		// switching the resampler over allocates and builds filter tables, so the work is
		// left to whoever drives the message thread. Until then the old rate keeps being
		// used, which costs a little pitch drift rather than a dropout.
		bool hasPendingDeviceSamplerate() const { return m_pendingDeviceSamplerate.load(std::memory_order_relaxed) > 0.0f; }
		bool applyPendingDeviceSamplerate();

	private:
		void processMidiClock(float _bpm, float _ppqPos, bool _isPlaying, size_t _sampleCount);
		float* getDummyBuffer(size_t _minimumSize);
		void updateDeviceLatency();
		// Composes the values the host is told about. The inputs only change while m_lock is
		// held, so publishing them here lets getLatency*() read without taking it - that read
		// happens once per audio block.
		void updateLatencies();
		void processMidiInEvents();
		void processMidiInEvent(const SMidiEvent& _ev);
		TransportDiscontinuity updateTransport(float _bpm, float _ppqPos, bool _isPlaying, bool _hasPpqPosition,
			size_t _sampleCount);
		void stampTransportGeneration(SMidiEvent& _event) const;

		dsp56k::RingBuffer<SMidiEvent, 1024, false> m_midiInRingBuffer;
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;

		SMidiEvent m_pendingSysexInput;

		ResamplerInOut m_resampler;
		mutable std::recursive_mutex m_lock;
		mutable std::mutex m_lockAddMidiEvent;

		Device* m_device;

		std::vector<float> m_dummyBuffer;

		float m_hostSamplerate = 0.0f;
		float m_hostSamplerateInv = 0.0f;

		uint32_t m_blockSize = 0;

		uint32_t m_deviceLatencyMidiToOutput = 0;
		uint32_t m_deviceLatencyInputToOutput = 0;

		std::atomic<uint32_t> m_latencyMidiToOutput{0};
		std::atomic<uint32_t> m_latencyInputToOutput{0};

		MidiClock m_midiClock;
		bool m_midiClockEnabled = true;

		uint32_t m_extraLatencyBlocks = 1;

		float m_deviceSamplerate = 0.0f;
		std::atomic<float> m_pendingDeviceSamplerate{0.0f};
		CallbackDeviceInvalid m_callbackDeviceInvalid;

		bool m_transportInitialized = false;
		bool m_lastIsPlaying = false;
		float m_lastBpm = 0.0f;
		float m_lastPpqPos = 0.0f;
		bool m_lastHasPpqPosition = false;
		size_t m_lastSampleCount = 0;
		std::atomic<uint32_t> m_transportGeneration{0};
	};
}
