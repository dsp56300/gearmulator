#include "deviceConnection.h"

#include "remoteDevice.h"
#include "dsp56kEmu/logging.h"
#include "networkLib/logging.h"

namespace bridgeClient
{
	static constexpr uint32_t g_replyTimeoutSecs = 10;

	DeviceConnection::DeviceConnection(RemoteDevice& _device, std::unique_ptr<networkLib::TcpStream>&& _stream) : TcpConnection(std::move(_stream)), m_device(_device)
	{
		m_handleReplyFunc = [](bridgeLib::Command, baseLib::BinaryStream&){};

		// send plugin description and device creation parameters, this will cause the server to either boot the device or ask for the rom if it doesn't have it yet
		send(bridgeLib::Command::PluginInfo, m_device.getPluginDesc());

		// do not send rom data now but only if the server asks for it
		sendDeviceCreateParams(false);
	}

	DeviceConnection::~DeviceConnection()
	{
		shutdown();
	}

	void DeviceConnection::handleCommand(const bridgeLib::Command _command, baseLib::BinaryStream& _in)
	{
		switch (_command)
		{
		case bridgeLib::Command::RequestRom:
			sendDeviceCreateParams(true);
			break;
		default:
			TcpConnection::handleCommand(_command, _in);
			break;
		}
	}

	void DeviceConnection::handleData(const bridgeLib::DeviceDesc& _desc)
	{
		m_deviceDesc = _desc;
		m_device.onBootFinished(_desc);
	}

	void DeviceConnection::handleDeviceInfo(baseLib::BinaryStream& _in)
	{
		TcpConnection::handleDeviceInfo(_in);
		m_handleReplyFunc(bridgeLib::Command::DeviceInfo, _in);
	}

	void DeviceConnection::handleException(const networkLib::NetException& _e)
	{
		m_device.onDisconnect();
	}

	void DeviceConnection::sendDeviceCreateParams(const bool _sendRom)
	{
		bridgeLib::DeviceCreateParams p;
		p.params = m_device.getDeviceCreateParams();
		if(!_sendRom)
			p.params.romData.clear();	
		send(bridgeLib::Command::DeviceCreateParams, p);
	}

	bool DeviceConnection::processAudio(const synthLib::TAudioInputs& _inputs, const synthLib::TAudioOutputs& _outputs, const uint32_t _size, const uint32_t _latency)
	{
		m_audioBuffers.writeInput(_inputs, _size);

		std::unique_lock lock(m_cvWaitMutex);

		const auto haveEnoughOutput = m_audioBuffers.getOutputSize() >= _size;

		m_audioBuffers.setLatency(_latency, haveEnoughOutput ? 0 : _size);

		const auto sendSize = m_audioBuffers.getInputSize();

		if(haveEnoughOutput)
		{
			lock.unlock();

			if(sendSize > 0)
				sendAudio(m_audioBuffers, m_device.getChannelCountIn(), sendSize);
			m_audioBuffers.readOutput(_outputs, _size);
		}
		else
		{
			assert(sendSize > 0);
			sendAudio(m_audioBuffers, m_device.getChannelCountIn(), sendSize);

			m_cvWait.wait_for(lock, std::chrono::seconds(g_replyTimeoutSecs), [this, _size]
			{
				return m_audioBuffers.getOutputSize() >= _size;
			});

			if(m_audioBuffers.getOutputSize() < _size)
			{
				LOG("Receive timeout, closing connection");
				close();
				return false;
			}

			m_audioBuffers.readOutput(_outputs, _size);
		}
		return true;
	}

	void DeviceConnection::handleAudio(baseLib::BinaryStream& _in)
	{
		{
			std::unique_lock lock(m_cvWaitMutex);
			TcpConnection::handleAudio(m_audioBuffers, _in);
		}

		m_cvWait.notify_one();
	}

	void DeviceConnection::handleMidi(const synthLib::SMidiEvent& _e)
	{
		m_midiOut.push_back(_e);
	}

	void DeviceConnection::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}

	bool DeviceConnection::getDeviceState(std::vector<uint8_t>& _state, synthLib::StateType _type)
	{
		return sendAwaitReply([this, _type]
		{
			bridgeLib::RequestDeviceState s;
			s.type = _type;
			send(bridgeLib::Command::RequestDeviceState, s);
		}, [&](baseLib::BinaryStream& _in)
		{
			const auto& s = TcpConnection::getDeviceState().state;
			_state.insert(_state.end(), s.begin(), s.end());
		}, bridgeLib::Command::DeviceState);
	}

	void DeviceConnection::handleDeviceState(baseLib::BinaryStream& _in)
	{
		TcpConnection::handleDeviceState(_in);
		m_handleReplyFunc(bridgeLib::Command::DeviceState, _in);
	}

	bool DeviceConnection::setDeviceState(const std::vector<uint8_t>& _state, const synthLib::StateType _type)
	{
		if(_state.empty())
			return false;

		auto& s = TcpConnection::getDeviceState();
		s.state = _state;
		s.type = _type;

		sendAwaitReply([&]
		{
			send(bridgeLib::Command::DeviceState, s);
		}, [](baseLib::BinaryStream&)
		{
		}, bridgeLib::Command::DeviceInfo);
		return true;
	}

	void DeviceConnection::setSamplerate(const float _samplerate)
	{
		sendAwaitReply([&]
		{
			bridgeLib::SetSamplerate sr;
			sr.samplerate = _samplerate;
			send(bridgeLib::Command::SetSamplerate, sr);
		}, [&](baseLib::BinaryStream&)
		{
		}, bridgeLib::Command::DeviceInfo);
	}

	void DeviceConnection::setStateFromUnknownCustomData(const std::vector<uint8_t>& _state)
	{
		sendAwaitReply([&]
		{
			bridgeLib::SetUnknownCustomData d;
			d.data = _state;
			send(bridgeLib::Command::SetUnknownCustomData, d);
		}, [&](baseLib::BinaryStream&)
		{
		}, bridgeLib::Command::DeviceInfo);
	}

	void DeviceConnection::setDspClockPercent(const uint32_t _percent)
	{
		sendAwaitReply([&]
		{
			bridgeLib::SetDspClockPercent p;
			p.percent = _percent;
			send(bridgeLib::Command::SetDspClockPercent, p);
		}, [&](baseLib::BinaryStream&)
		{
		}, bridgeLib::Command::DeviceInfo);
	}

	bool DeviceConnection::sendAwaitReply(const std::function<void()>& _send, const std::function<void(baseLib::BinaryStream&)>& _reply, const bridgeLib::Command _replyCommand)
	{
		bool receiveDone = false;

		m_handleReplyFunc = [&](const bridgeLib::Command _command, baseLib::BinaryStream& _in)
		{
			if(_command != _replyCommand)
				return;

			_reply(_in);

			{
				std::unique_lock lockCv(m_cvWaitMutex);
				receiveDone = true;
			}
			m_cvWait.notify_one();
		};

		_send();

		std::unique_lock lockCv(m_cvWaitMutex);
		m_cvWait.wait_for(lockCv, std::chrono::seconds(g_replyTimeoutSecs), [&]
		{
			return receiveDone;
		});

		m_handleReplyFunc = [](bridgeLib::Command, baseLib::BinaryStream&)
		{
		};

		if(receiveDone)
			return true;
		LOG("Receive timeout, closing connection");
		close();
		return false;
	}
}
