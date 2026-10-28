#pragma once

#include <atomic>
#include <mutex>
#include <functional>

#include "midiTypes.h"
#include "resamplerInOut.h"

#include "dsp56kBase/ringbuffer.h"

#include "deviceTypes.h"
#include "midiClock.h"

namespace synthLib
{
	class Device;

	// The loop the host plays, in quarter notes. A host that does not end its block at the loop end reports the
	// jump back only with the next block, by then the first events of the new pass are already queued as part of
	// the old one. Knowing the loop, Plugin::process() puts the jump on the sample it happens at.
	struct HostLoop
	{
		bool active = false;
		double ppqStart = 0.0;
		double ppqEnd = 0.0;
	};

	class Plugin
	{
	public:
		// Called on the audio thread, once per device, when the device stops being valid (a remote device lost its
		// connection, for example). Replacing it takes a network round trip or a boot, so the callback only arranges
		// for that elsewhere and hands the new device in through setDevice(); meanwhile process() outputs silence.
		using CallbackDeviceInvalid = std::function<void(Device*)>;

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

		void process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _count, float _bpm, double _ppqPos,
			bool _isPlaying, bool _hasPpqPosition, const HostLoop& _loop = {});
		void getMidiOut(std::vector<SMidiEvent>& _midiOut);

		bool isValid() const;

		void setDevice(Device* _device);
		Device* getDevice() const { return m_device; }

		// The device's clock is set with the processing lock held, so that it never changes in the middle of process()
		bool setDspClockPercent(uint32_t _percent);

		bool getState(std::vector<uint8_t>& _state, StateType _type);
		bool setState(const std::vector<uint8_t>& _state);
		void insertMidiEvent(const SMidiEvent& _ev);

		bool setLatencyBlocks(uint32_t _latencyBlocks);
		uint32_t getLatencyBlocks() const { return m_extraLatencyBlocks; }

		// Turn this off in a host that has no transport to derive a clock from - a standalone
		// application, say. MidiClock::process() advances its tick counter from _bpm alone and does
		// not consult _isPlaying, so a host that passes a fixed bpm would otherwise flood the device
		// with timing clocks forever. Set it on every Plugin you build: replacing a device builds a
		// new one, and forgetting it there fails silently.
		void setMidiClockEnabled(bool _enabled);

		// A device whose firmware retunes its clock is noticed on the audio thread, but
		// switching the resampler over allocates and builds filter tables, so the work is
		// left to whoever drives the message thread. Until then the old rate keeps being
		// used, which costs a little pitch drift rather than a dropout.
		bool hasPendingDeviceSamplerate() const
		{
			return m_pendingDeviceSamplerate.load(std::memory_order_relaxed) > 0.0f;
		}
		bool applyPendingDeviceSamplerate();

	private:
		void processMidiClock(float _bpm, double _ppqPos, bool _isPlaying, size_t _sampleCount);
		float* getDummyBuffer(size_t _minimumSize);
		const float* getSilence(size_t _minimumSize);
		void updateDeviceLatency();
		// Composes the values the host is told about. The inputs only change while m_lock is
		// held, so publishing them here lets getLatency*() read without taking it - that read
		// happens once per audio block.
		void updateLatencies();
		void processMidiInEvents();
		void processMidiInEvent(const SMidiEvent& _ev);
		TransportDiscontinuity updateTransport(float _bpm, double _ppqPos, bool _isPlaying, bool _hasPpqPosition,
			size_t _sampleCount);
		void insertLoopWraps(float _bpm, double _ppqPos, bool _isPlaying, bool _hasPpqPosition, size_t _sampleCount,
			const HostLoop& _loop);
		void stampTransportGeneration(SMidiEvent& _event) const;

		dsp56k::RingBuffer<SMidiEvent, 1024, false> m_midiInRingBuffer;
		std::vector<SMidiEvent> m_midiIn;
		std::vector<SMidiEvent> m_midiOut;

		SMidiEvent m_pendingSysexInput;

		ResamplerInOut m_resampler;
		mutable std::recursive_mutex m_lock;
		// addMidiEvent() takes m_lock while holding this one, so nothing may wait for this one while holding m_lock:
		// whoever needs both takes them in one std::scoped_lock. Recursive because setDevice() does that and then
		// calls setState(), which takes both again.
		mutable std::recursive_mutex m_lockAddMidiEvent;

		Device* m_device;

		std::vector<float> m_dummyBuffer;
		// Missing inputs read this one, never m_dummyBuffer: missing outputs write into that, and
		// sharing it fed the device its own output. Only ever grown, with zeros.
		std::vector<float> m_silence;

		float m_hostSamplerate = 0.0f;
		float m_hostSamplerateInv = 0.0f;

		uint32_t m_blockSize = 0;

		// In host samples, unrounded: updateLatencies() rounds the sum once
		float m_deviceLatencyMidiToOutput = 0.0f;
		float m_deviceLatencyInputToOutput = 0.0f;

		std::atomic<uint32_t> m_latencyMidiToOutput{0};
		std::atomic<uint32_t> m_latencyInputToOutput{0};

		MidiClock m_midiClock;
		bool m_midiClockEnabled = true;

		uint32_t m_extraLatencyBlocks = 1;

		float m_deviceSamplerate = 0.0f;
		std::atomic<float> m_pendingDeviceSamplerate{0.0f};
		CallbackDeviceInvalid m_callbackDeviceInvalid;
		bool m_deviceInvalidReported = false;

		bool m_transportInitialized = false;
		bool m_lastIsPlaying = false;
		float m_lastBpm = 0.0f;
		double m_lastPpqPos = 0.0;
		bool m_lastHasPpqPosition = false;
		size_t m_lastSampleCount = 0;
		// How far the loop wraps placed in the last block moved the position back, in quarter notes. The next
		// block starts that much earlier than a straight run would, and that is not a jump of its own.
		double m_loopWrapPpq = 0.0;
		std::atomic<uint32_t> m_transportGeneration{0};
	};
}
