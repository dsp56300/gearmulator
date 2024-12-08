#pragma once

#include <mutex>

#include "bridgeLib/tcpConnection.h"
#include "networkLib/networkThread.h"
#include "networkLib/tcpStream.h"
#include "synthLib/device.h"

namespace bridgeServer
{
	class Server;

	class ClientConnection : public bridgeLib::TcpConnection
	{
	public:
		ClientConnection(Server& _server, std::unique_ptr<networkLib::TcpStream>&& _stream, std::string _name);
		~ClientConnection() override;

		void handleMidi(const synthLib::SMidiEvent& _e) override;
		void handleData(const bridgeLib::PluginDesc& _desc) override;
		void handleData(const bridgeLib::DeviceCreateParams& _params) override;
		void handleData(const bridgeLib::SetSamplerate& _params) override;
		void handleData(const bridgeLib::SetDspClockPercent& _params) override;
		void handleData(const bridgeLib::SetUnknownCustomData& _params) override;

		void handleAudio(baseLib::BinaryStream& _in) override;
		void sendDeviceState(synthLib::StateType _type);
		void handleRequestDeviceState(bridgeLib::RequestDeviceState& _requestDeviceState) override;
		void handleDeviceState(bridgeLib::DeviceState& _in) override;
		void handleDeviceState(baseLib::BinaryStream& _in) override;
		void handleException(const networkLib::NetException& _e) override;

		const auto& getPluginDesc() const { return m_pluginDesc; }

	private:
		void sendDeviceInfo();
		void createDevice();
		void destroyDevice();

		void errorClose(bridgeLib::ErrorCode _code, const std::string& _err);

		Server& m_server;
		std::string m_name;

		bridgeLib::PluginDesc m_pluginDesc;
		synthLib::DeviceCreateParams m_deviceCreateParams;

		synthLib::Device* m_device = nullptr;

		synthLib::TAudioInputs m_audioInputs;
		synthLib::TAudioOutputs m_audioOutputs;

		std::array<std::vector<float>, std::tuple_size_v<synthLib::TAudioInputs>> m_audioInputBuffers;
		std::array<std::vector<float>, std::tuple_size_v<synthLib::TAudioOutputs>> m_audioOutputBuffers;
		std::vector<synthLib::SMidiEvent> m_midiIn;
		std::vector<synthLib::SMidiEvent> m_midiOut;

		bool m_romRequested = false;

		std::mutex m_mutexDeviceState;
	};
}
