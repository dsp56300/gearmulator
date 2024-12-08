#pragma once

#include <condition_variable>
#include <functional>
#include <memory>

#include "bridgeLib/commands.h"

#include "synthLib/device.h"

namespace bridgeLib
{
	struct PluginDesc;
}

namespace bridgeClient
{
	class DeviceConnection;

	class RemoteDevice : public synthLib::Device
	{
	public:
		RemoteDevice(const synthLib::DeviceCreateParams& _params, bridgeLib::PluginDesc&& _desc, const std::string& _host = {}, uint32_t _port = 0);
		~RemoteDevice() override;
		RemoteDevice(RemoteDevice&&) = delete;
		RemoteDevice(const RemoteDevice&) = delete;
		RemoteDevice& operator = (RemoteDevice&&) = delete;
		RemoteDevice& operator = (const RemoteDevice&) = delete;

		float getSamplerate() const override;
		bool isValid() const override;
		bool getState(std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		bool setState(const std::vector<uint8_t>& _state, synthLib::StateType _type) override;
		uint32_t getChannelCountIn() override;
		uint32_t getChannelCountOut() override;
		bool setDspClockPercent(uint32_t _percent) override;
		uint32_t getDspClockPercent() const override;
		uint64_t getDspClockHz() const override;
		uint32_t getInternalLatencyInputToOutput() const override;
		uint32_t getInternalLatencyMidiToOutput() const override;
		void getPreferredSamplerates(std::vector<float>& _dst) const override;
		void getSupportedSamplerates(std::vector<float>& _dst) const override;

		const auto& getPluginDesc() const { return m_pluginDesc; }
		auto& getPluginDesc() { return m_pluginDesc; }

		bool setSamplerate(float _samplerate) override;

		bool setStateFromUnknownCustomData(const std::vector<uint8_t>& _state) override;

		void onBootFinished(const bridgeLib::DeviceDesc& _desc);
		void onDisconnect();

	protected:
		void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut) override;
		void processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, size_t _samples) override;
		bool sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response) override;

		bool safeCall(const std::function<bool()>& _func, const std::function<void()>& _onFail = [] {});

		void createConnection(const std::string& _host, uint32_t _port);

		bridgeLib::PluginDesc m_pluginDesc;
		bridgeLib::DeviceDesc m_deviceDesc;
		std::unique_ptr<DeviceConnection> m_connection;

		std::mutex m_cvWaitMutex;
		std::condition_variable m_cvWait;
		bool m_valid = false;
	};
}
