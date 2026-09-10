#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "audioTypes.h"
#include "deviceTypes.h"

#include "midiTypes.h"
#include "buildconfig.h"
#include "midiTranslator.h"

#include "baseLib/compilerdefs.h"
#include "baseLib/md5.h"

namespace synthLib
{
	struct DeviceCreateParams
	{
		float preferredSamplerate = 0.0f;
		float hostSamplerate = 0.0f;
		std::string romName;
		std::vector<uint8_t> romData;
		baseLib::MD5 romHash;
		uint32_t customData = 0;
		std::string homePath;
	};

	class Device
	{
	public:
		Device(const DeviceCreateParams& _params);
		Device(const Device&) = delete;
		Device(Device&&) = delete;

		virtual ~Device();

		Device& operator = (const Device&) = delete;
		Device& operator = (Device&&) = delete;

		virtual void process(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _size, const std::vector<SMidiEvent>& _midiIn, std::vector<SMidiEvent>& _midiOut);

		void setExtraLatencySamples(uint32_t _size);
		uint32_t getExtraLatencySamples() const { return m_extraLatency; }

		virtual uint32_t getInternalLatencyMidiToOutput() const { return 0; }
		virtual uint32_t getInternalLatencyInputToOutput() const { return 0; }

		virtual void getSupportedSamplerates(std::vector<float>& _dst) const
		{
			_dst.push_back(getSamplerate());
		}
		virtual float getSamplerate() const = 0;
		// Rates selected by firmware during processing, rather than by the host.
		// Declare these so the plugin can prepare conversion filters in advance.
		// Every rate this device can switch to *while running*, i.e. because its own firmware
		// changed the clock - a front panel menu selecting 44.1 vs 48 kHz, say. Nothing here means
		// "my rate never moves", which is true of every device that picks a rate at construction.
		//
		// Declaring them is what makes such a change cheap. Plugin::process() notices the new rate
		// and hands it to the message thread (see applyPendingDeviceSamplerate), which switches the
		// resampler over; a rate listed here already has its converters built and prewarmed, so the
		// switch allocates nothing. A rate that is not listed has to build them on the spot.
		//
		// Note the rate itself is reported by getSamplerate(): this list only says which values it
		// may take, so keep the two in step.
		virtual void getDynamicSamplerates(std::vector<float>& _dst) const {}
		virtual void getPreferredSamplerates(std::vector<float>& _dst) const
		{
			return getSupportedSamplerates(_dst);
		}

		bool isSamplerateSupported(const float& _samplerate) const;

		virtual bool setSamplerate(float _samplerate);

		float getDeviceSamplerate(float _preferredDeviceSamplerate, float _hostSamplerate) const;
		float getDeviceSamplerateForHostSamplerate(float _hostSamplerate) const;

		auto& getDeviceCreateParams() { return m_createParams; }
		const auto& getDeviceCreateParams() const { return m_createParams; }

		virtual bool isValid() const = 0;

#if SYNTHLIB_DEMO_MODE == 0
		virtual bool getState(std::vector<uint8_t>& _state, StateType _type) = 0;
		virtual bool setState(const std::vector<uint8_t>& _state, StateType _type) = 0;
		virtual bool setStateFromUnknownCustomData(const std::vector<uint8_t> &_state) { return false; }
#endif

		virtual uint32_t getChannelCountIn() = 0;
		virtual uint32_t getChannelCountOut() = 0;

		virtual bool setDspClockPercent(uint32_t _percent = 100) = 0;
		virtual uint32_t getDspClockPercent() const = 0;
		virtual uint64_t getDspClockHz() const = 0;
		virtual bool canModifyDspClock() const { return false; }

		BASELIB_NOINLINE virtual void release(std::vector<SMidiEvent>& _events);

		auto& getMidiTranslator() { return m_midiTranslator; }

	protected:
		virtual void readMidiOut(std::vector<SMidiEvent>& _midiOut) = 0;
		virtual void processAudio(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _samples) = 0;
		virtual bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>& _response) = 0;

		// Host transport start / stop / seek. This is a marker, not MIDI: it carries no
		// status or data bytes, so it must never be handed to sendMidi, where a device
		// pushes an event's bytes into its emulated UART - a lone 0x00 there completes the
		// firmware's running-status message and fakes a Program Change. Devices that rate
		// limit their MIDI input override this to pass the generation on; for everyone
		// else it is a no-op.
		virtual void onTransportDiscontinuity(const SMidiEvent& /*_ev*/) {}

		void dummyProcess(uint32_t _numSamples);

	private:
		DeviceCreateParams m_createParams;
		std::vector<SMidiEvent> m_midiIn;

		uint32_t m_extraLatency = 0;

		MidiTranslator m_midiTranslator;
		std::vector<SMidiEvent> m_translatorOut;
	};
}
