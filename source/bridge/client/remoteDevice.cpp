#include "remoteDevice.h"

#include "udpClient.h"

#include "bridgeLib/types.h"

#include "dsp56kEmu/logging.h"

#include "networkLib/tcpClient.h"
#include "networkLib/tcpStream.h"

#include "synthLib/deviceException.h"

#include "deviceConnection.h"

#include <condition_variable>

#include "networkLib/exception.h"
#include "networkLib/logging.h"

#include "synthLib/os.h"

namespace bridgeClient
{
	static constexpr uint32_t g_udpTimeout = 5;	// seconds
	static constexpr uint32_t g_tcpTimeout = 5;	// seconds

	RemoteDevice::RemoteDevice(const synthLib::DeviceCreateParams& _params, bridgeLib::PluginDesc&& _desc, const std::string& _host/* = {}*/, uint32_t _port/* = 0*/) : Device(_params), m_pluginDesc(std::move(_desc))
	{
		getDeviceCreateParams().romHash = baseLib::MD5(getDeviceCreateParams().romData);
		getDeviceCreateParams().romName = synthLib::getFilenameWithoutPath(getDeviceCreateParams().romName);

		m_pluginDesc.protocolVersion = bridgeLib::g_protocolVersion;
		createConnection(_host, _port);
	}

	RemoteDevice::~RemoteDevice()
	{
		m_connection.reset();
	}

	float RemoteDevice::getSamplerate() const
	{
		return m_deviceDesc.samplerate;
	}

	bool RemoteDevice::isValid() const
	{
		return m_valid;
	}

	bool RemoteDevice::getState(std::vector<uint8_t>& _state, const synthLib::StateType _type)
	{
		return safeCall([&]
		{
			return m_connection->getDeviceState(_state, _type);
		}, [&]
		{
			// if there is no valid connection anymore attempt to grab the latest state that was sent
			if(!m_connection)
				return;
			auto& state = static_cast<bridgeLib::TcpConnection*>(m_connection.get())->getDeviceState();
			_state.insert(_state.end(), state.state.begin(), state.state.end());
		});
	}

	bool RemoteDevice::setState(const std::vector<uint8_t>& _state, const synthLib::StateType _type)
	{
		if(!isValid())
			return false;
		return m_connection->setDeviceState(_state, _type);
	}

	uint32_t RemoteDevice::getChannelCountIn()
	{
		return m_deviceDesc.inChannels;
	}

	uint32_t RemoteDevice::getChannelCountOut()
	{
		return m_deviceDesc.outChannels;
	}

	bool RemoteDevice::setDspClockPercent(const uint32_t _percent)
	{
		return safeCall([&]
		{
			m_connection->setDspClockPercent(_percent);
			return true;
		});
	}

	uint32_t RemoteDevice::getDspClockPercent() const
	{
		return m_deviceDesc.dspClockPercent;
	}

	uint64_t RemoteDevice::getDspClockHz() const
	{
		return m_deviceDesc.dspClockHz;
	}

	uint32_t RemoteDevice::getInternalLatencyInputToOutput() const
	{
		return m_deviceDesc.latencyInToOut;
	}

	uint32_t RemoteDevice::getInternalLatencyMidiToOutput() const
	{
		return m_deviceDesc.latencyMidiToOut;
	}

	void RemoteDevice::getPreferredSamplerates(std::vector<float>& _dst) const
	{
		_dst = m_deviceDesc.preferredSamplerates;
	}

	void RemoteDevice::getSupportedSamplerates(std::vector<float>& _dst) const
	{
		_dst = m_deviceDesc.supportedSamplerates;
	}

	bool RemoteDevice::setSamplerate(const float _samplerate)
	{
		return safeCall([&]
		{
			m_connection->setSamplerate(_samplerate);
			return true;
		});
	}

	bool RemoteDevice::setStateFromUnknownCustomData(const std::vector<uint8_t>& _state)
	{
		return safeCall([&]
		{
			m_connection->setStateFromUnknownCustomData(_state);
			return true;
		});
	} 

	void RemoteDevice::onBootFinished(const bridgeLib::DeviceDesc& _desc)
	{
		{
			std::unique_lock lock(m_cvWaitMutex);
			m_deviceDesc = _desc;
		}
		m_cvWait.notify_one();
	}

	void RemoteDevice::onDisconnect()
	{
		{
			std::unique_lock lock(m_cvWaitMutex);
			m_valid = false;
		}
		m_cvWait.notify_one();
	}

	void RemoteDevice::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		safeCall([&]
		{
			m_connection->readMidiOut(_midiOut);
			return true;
		});
	}

	void RemoteDevice::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		safeCall([&]
		{
			return m_connection->processAudio(_inputs, _outputs, static_cast<uint32_t>(_samples), getExtraLatencySamples());
		});
	}

	bool RemoteDevice::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& _response)
	{
		return safeCall([&]
		{
			return m_connection->send(_ev);
		});
	}

	bool RemoteDevice::safeCall(const std::function<bool()>& _func, const std::function<void()>& _onFail/* = [] {}*/)
	{
		if(!isValid())
		{
			_onFail();
			return false;
		}

		try
		{
			if(!_func())
			{
				onDisconnect();
				_onFail();
			}
		}
		catch(networkLib::NetException& e)
		{
			LOG(e.what());
			onDisconnect();
			_onFail();
		}
		return true;
	}

	void RemoteDevice::createConnection(const std::string& _host, uint32_t _port)
	{
		// wait for UDP connection up to 5 seconds
		bridgeLib::ServerInfo si;
		std::string host = _host;

		if(_host.empty() || !_port)
		{
			UdpClient udpClient(m_pluginDesc, [&](const std::string& _hostname, const bridgeLib::ServerInfo& _si, const bridgeLib::Error& _err)
			{
				if(_err.code != bridgeLib::ErrorCode::Ok)
					return;
				{
					std::unique_lock lock(m_cvWaitMutex);
					si = _si;
					host = _hostname;
					LOG("Found server "<< _hostname << ':' << si.portTcp);
				}
				m_cvWait.notify_one();
			});

			std::unique_lock lockCv(m_cvWaitMutex);
			if(!m_cvWait.wait_for(lockCv, std::chrono::seconds(g_udpTimeout), [&si]
			{
				return si.protocolVersion == bridgeLib::g_protocolVersion;
			}))
			{
				throw synthLib::DeviceException(synthLib::DeviceError::RemoteUdpConnectFailed, "No server found");
			}
		}
		else
		{
			si.portTcp = _port;
		}

		// wait for TCP connection for another 5 seconds
		std::unique_ptr<networkLib::TcpStream> stream;

		LOG("Connecting to "<< host << ':' << si.portTcp);

		networkLib::TcpClient client(host, si.portTcp,[&](std::unique_ptr<networkLib::TcpStream> _tcpStream)
		{
			{
				std::unique_lock lock(m_cvWaitMutex);
				stream = std::move(_tcpStream);
			}
			m_cvWait.notify_one();
		});

		{
			std::unique_lock lockCv(m_cvWaitMutex);
			if(!m_cvWait.wait_for(lockCv, std::chrono::seconds(g_tcpTimeout), [&stream]
			{
				return stream.get() && stream->isValid();
			}))
			{
				throw synthLib::DeviceException(synthLib::DeviceError::RemoteTcpConnectFailed, "Failed to connect to " + host + ':' + std::to_string(si.portTcp));
			}
		}

		// we are connected. Wait for device info as long as the connection is alive. The server will either
		// close it if the requirements are not fulfilled (plugin not existing on server) or will eventually
		// send device info after the device has bene opened on the server
		m_connection.reset(new DeviceConnection(*this, std::move(stream)));

		std::unique_lock lockCv(m_cvWaitMutex);
		m_cvWait.wait(lockCv, [this]()
		{
			// continue waiting if the connection is still active but we didn't receive a device desc yet
			return !m_connection->isValid() || m_deviceDesc.outChannels > 0;
		});

		if(!m_connection->isValid() || m_deviceDesc.outChannels == 0)
			throw synthLib::DeviceException(synthLib::DeviceError::RemoteTcpConnectFailed);

		m_valid = true;

		LOG("Connection established successfully");
	}
}
