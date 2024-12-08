#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "audioTypes.h"
#include "deviceTypes.h"

#include "midiTypes.h"
#include "buildconfig.h"
#include "midiTranslator.h"

#include "asmjit/core/api-config.h"

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


		ASMJIT_NOINLINE virtual void release(std::vector<SMidiEvent>& _events);

		auto& getMidiTranslator() { return m_midiTranslator; }

	protected:
		virtual void readMidiOut(std::vector<SMidiEvent>& _midiOut) = 0;
		virtual void processAudio(const TAudioInputs& _inputs, const TAudioOutputs& _outputs, size_t _samples) = 0;
		virtual bool sendMidi(const SMidiEvent& _ev, std::vector<SMidiEvent>& _response) = 0;

		void dummyProcess(uint32_t _numSamples);

	private:
		DeviceCreateParams m_createParams;
		std::vector<SMidiEvent> m_midiIn;

		uint32_t m_extraLatency = 0;

		MidiTranslator m_midiTranslator;
		std::vector<SMidiEvent> m_translatorOut;
	};
}
