#include "clientConnection.h"

#include "server.h"
#include "bridgeLib/error.h"
#include "networkLib/logging.h"

namespace bridgeServer
{
	static constexpr uint32_t g_audioBufferSize = 16384;

	ClientConnection::ClientConnection(Server& _server, std::unique_ptr<networkLib::TcpStream>&& _stream, std::string _name)
		: TcpConnection(std::move(_stream))
		, m_server(_server)
		, m_name(std::move(_name))
	{
		for(size_t i=0; i<m_audioInputBuffers.size(); ++i)
		{
			auto& buf = m_audioInputBuffers[i];
			buf.resize(g_audioBufferSize);
			m_audioInputs[i] = buf.data();
		}

		for(size_t i=0; i<m_audioOutputBuffers.size(); ++i)
		{
			auto& buf = m_audioOutputBuffers[i];
			buf.resize(g_audioBufferSize);
			m_audioOutputs[i] = buf.data();
		}

		m_midiIn.reserve(1024);
		m_midiOut.reserve(4096);

		getDeviceState().state.reserve(8 * 1024 * 1024);
	}

	ClientConnection::~ClientConnection()
	{
		shutdown();
		destroyDevice();
	}

	void ClientConnection::handleMidi(const synthLib::SMidiEvent& _e)
	{
		m_midiIn.push_back(_e);
	}

	void ClientConnection::handleData(const bridgeLib::PluginDesc& _desc)
	{
		m_pluginDesc = _desc;
		LOGNET(networkLib::LogLevel::Info, "Client " << m_name << " identified as plugin " << _desc.pluginName << ", version " << _desc.pluginVersion);
		m_name = m_pluginDesc.pluginName + '-' + m_name;
		createDevice();
	}

	void ClientConnection::handleData(const bridgeLib::DeviceCreateParams& _params)
	{
		const auto& p = _params.params;

		if(p.romData.empty())
		{
			// if no rom data has been transmitted, try to load from cache
			const auto& romData = m_server.getRomPool().getRom(p.romHash);

			if(!romData.empty())
			{
				// if we have the ROM, we are good to go
				LOGNET(networkLib::LogLevel::Info, "ROM " << p.romName << " with hash " << p.romHash.toString() << " found, transfer skipped");

				m_deviceCreateParams = p;
				m_deviceCreateParams.romData = romData;

				createDevice();
			}
			else
			{
				if(m_romRequested)
				{
					LOGNET(networkLib::LogLevel::Warning, "Client " << m_name << " failed to provide rom that we asked for, disconnecting");
					close();
					return;
				}

				// If not, ask client to send it
				send(bridgeLib::Command::RequestRom);
				LOGNET(networkLib::LogLevel::Info, "ROM " << p.romName << " with hash " << p.romHash.toString() << " not found, requesting client to send it");
				m_romRequested = true;
			}
		}
		else
		{
			// client sent the rom. Validate transmission by comparing hashes
			const baseLib::MD5 calculatedHash(p.romData);
			if(calculatedHash != p.romHash)
			{
				LOGNET(networkLib::LogLevel::Error, "Calculated hash " << calculatedHash.toString() << " of ROM " << p.romName << " does not match sent hash " <<  p.romHash.toString() << ", transfer error");
				close();
			}

			LOGNET(networkLib::LogLevel::Info, "Adding ROM " << p.romName << " with hash " << p.romHash.toString() << " to pool");
			m_server.getRomPool().addRom(p.romName, p.romData);

			m_deviceCreateParams = p;

			createDevice();
		}
	}

	void ClientConnection::handleAudio(baseLib::BinaryStream& _in)
	{
		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::UnexpectedCommand, "Audio data without valid device");
			return;
		}

		const auto numSamples = TcpConnection::handleAudio(const_cast<float* const*>(m_audioInputs.data()), _in);

		m_device->process(m_audioInputs, m_audioOutputs, numSamples, m_midiIn, m_midiOut);

		for (const auto& midiOut : m_midiOut)
			send(midiOut);

		sendAudio(m_audioOutputs.data(), std::min(static_cast<uint32_t>(m_audioOutputs.size()), m_device->getChannelCountOut()), numSamples);

		m_midiIn.clear();

		m_device->release(m_midiOut);
	}

	void ClientConnection::sendDeviceState(const synthLib::StateType _type)
	{
		if(!m_device)
			return;

		std::scoped_lock lock(m_mutexDeviceState);
		auto& state = getDeviceState();
		state.type = _type;

		state.state.clear();
		m_device->getState(state.state, _type);

		send(bridgeLib::Command::DeviceState, state);
	}

	void ClientConnection::handleRequestDeviceState(bridgeLib::RequestDeviceState& _requestDeviceState)
	{
		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::UnexpectedCommand, "Device state request without valid device");
			return;
		}

		sendDeviceState(_requestDeviceState.type);
	}

	void ClientConnection::handleDeviceState(bridgeLib::DeviceState& _in)
	{
		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::UnexpectedCommand, "Device state without valid device");
			return;
		}

		m_device->setState(_in.state, _in.type);
		sendDeviceInfo();
	}

	void ClientConnection::handleDeviceState(baseLib::BinaryStream& _in)
	{
		std::scoped_lock lock(m_mutexDeviceState);
		TcpConnection::handleDeviceState(_in);
	}

	void ClientConnection::handleException(const networkLib::NetException& _e)
	{
		exit(true);
		m_server.onClientException(*this, _e);
	}

	void ClientConnection::handleData(const bridgeLib::SetSamplerate& _params)
	{
		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::UnexpectedCommand, "Set samplerate request without valid device");
			return;
		}

		m_device->setSamplerate(_params.samplerate);
		sendDeviceInfo();
	}

	void ClientConnection::handleData(const bridgeLib::SetDspClockPercent& _params)
	{
		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::UnexpectedCommand, "Set DSP clock request without valid device");
			return;
		}

		m_device->setDspClockPercent(_params.percent);
		sendDeviceInfo();
	}

	void ClientConnection::handleData(const bridgeLib::SetUnknownCustomData& _params)
	{
		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::UnexpectedCommand, "Set custom data request without valid device");
			return;
		}

		m_device->setStateFromUnknownCustomData(_params.data);
		sendDeviceInfo();
	}

	void ClientConnection::sendDeviceInfo()
	{
		bridgeLib::DeviceDesc deviceDesc;

		deviceDesc.samplerate = m_device->getSamplerate();
		deviceDesc.outChannels = m_device->getChannelCountOut();
		deviceDesc.inChannels = m_device->getChannelCountIn();
		deviceDesc.dspClockPercent = m_device->getDspClockPercent();
		deviceDesc.dspClockHz = m_device->getDspClockHz();
		deviceDesc.latencyInToOut = m_device->getInternalLatencyInputToOutput();
		deviceDesc.latencyMidiToOut = m_device->getInternalLatencyMidiToOutput();

		deviceDesc.preferredSamplerates.reserve(64);
		deviceDesc.supportedSamplerates.reserve(64);

		m_device->getPreferredSamplerates(deviceDesc.preferredSamplerates);
		m_device->getSupportedSamplerates(deviceDesc.supportedSamplerates);

		send(bridgeLib::Command::DeviceInfo, deviceDesc);
	}

	void ClientConnection::createDevice()
	{
		if(m_pluginDesc.pluginVersion == 0 || m_deviceCreateParams.romData.empty())
			return;

		m_device = m_server.getPlugins().createDevice(m_deviceCreateParams, m_pluginDesc);

		if(!m_device)
		{
			errorClose(bridgeLib::ErrorCode::FailedToCreateDevice,"Failed to create device");
			return;
		}

		const auto& d = m_pluginDesc;
		LOGNET(networkLib::LogLevel::Info, "Created new device for plugin '" << d.pluginName << "', version " << d.pluginVersion << ", id " << d.plugin4CC);

		// recover a previously lost connection if possible
		const auto cachedDeviceState = m_server.getCachedDeviceState(d.sessionId);

		if(cachedDeviceState.isValid())
		{
			LOGNET(networkLib::LogLevel::Info, m_name << ": Recovering previous device state for session id " << d.sessionId);
			send(bridgeLib::Command::DeviceState, cachedDeviceState);
			m_device->setState(cachedDeviceState.state, cachedDeviceState.type);
		}

		sendDeviceInfo();
	}

	void ClientConnection::destroyDevice()
	{
		if(!m_device)
			return;

		if(isValid())
		sendDeviceState(synthLib::StateTypeGlobal);

		m_server.getPlugins().destroyDevice(m_pluginDesc, m_device);
		m_device = nullptr;
	}

	void ClientConnection::errorClose(const bridgeLib::ErrorCode _code, const std::string& _err)
	{
		LOGNET(networkLib::LogLevel::Error, m_name + ": " + _err);

		bridgeLib::Error err;
		err.code = _code;
		err.msg = _err;

		send(bridgeLib::Command::Error, err);
		std::this_thread::sleep_for(std::chrono::seconds(1));
		close();
	}
}
