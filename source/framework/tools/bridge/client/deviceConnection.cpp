#include "deviceConnection.h"

#include "remoteDevice.h"
#include "dsp56kBase/logging.h"
#include "networkLib/logging.h"

namespace bridgeClient
{
	static constexpr uint32_t g_replyTimeoutSecs = 10;

	DeviceConnection::DeviceConnection(RemoteDevice& _device, std::unique_ptr<networkLib::TcpStream>&& _stream) : TcpConnection(std::move(_stream)), m_device(_device)
	{
		// send plugin description and device creation parameters, this will cause the server to either boot the device or ask for the rom if it doesn't have it yet
		send(bridgeLib::Command::PluginInfo, m_device.getPluginDesc());

		// do not send rom data now but only if the server asks for it
		sendDeviceCreateParams(false);

		// the server answers once the thread runs, which dispatches to us and needs our members
		start();
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

	void DeviceConnection::handleData(const bridgeLib::Error& _error)
	{
		m_device.onServerError(_error);
	}

	void DeviceConnection::handleDeviceInfo(baseLib::BinaryStream& _in)
	{
		TcpConnection::handleDeviceInfo(_in);
		onReply(bridgeLib::Command::DeviceInfo, _in);
	}

	void DeviceConnection::handleException(const networkLib::NetException& _e)
	{
		// Nothing arrives any more, so nobody waits for audio or a reply until the timeout. Taking each mutex
		// once makes sure that a waiter either sees the flag or already waits when it is notified.
		m_connectionLost = true;

		{
			std::lock_guard lock(m_audioMutex);
		}
		m_audioCv.notify_all();

		{
			std::lock_guard lock(m_replyMutex);
		}
		m_replyCv.notify_all();

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

		std::unique_lock lock(m_audioMutex);

		const auto haveEnoughOutput = m_audioBuffers.getOutputSize() >= _size;

		m_audioBuffers.setLatency(_latency, haveEnoughOutput ? 0 : _size);

		const auto sendSize = m_audioBuffers.getInputSize();

		// Not with the lock held: the write may block until the server reads, the server may wait for us to read
		// its output, and our network thread needs the lock to take it.
		lock.unlock();

		assert(haveEnoughOutput || sendSize > 0);

		if(sendSize > 0)
			sendAudio(m_audioBuffers, m_device.getChannelCountIn(), sendSize);

		lock.lock();

		m_audioCv.wait_for(lock, std::chrono::seconds(g_replyTimeoutSecs), [this, _size]
		{
			return m_connectionLost || m_audioBuffers.getOutputSize() >= _size;
		});

		if(m_audioBuffers.getOutputSize() < _size)
		{
			lock.unlock();

			if(!m_connectionLost)
			{
				LOG("Receive timeout, closing connection");
				close();
			}
			return false;
		}

		// With the lock held, the network thread adds to the output size while we take from it
		m_audioBuffers.readOutput(_outputs, _size);
		return true;
	}

	void DeviceConnection::handleAudio(baseLib::BinaryStream& _in)
	{
		{
			std::lock_guard lock(m_audioMutex);
			TcpConnection::handleAudio(m_audioBuffers, _in);
		}

		m_audioCv.notify_all();
	}

	void DeviceConnection::handleMidi(const synthLib::SMidiEvent& _e)
	{
		std::lock_guard lock(m_midiOutMutex);
		m_midiOut.push_back(_e);
	}

	void DeviceConnection::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		std::lock_guard lock(m_midiOutMutex);
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
		}, [&](baseLib::BinaryStream&)
		{
			// runs in handleDeviceState(), which holds m_deviceStateMutex
			const auto& s = TcpConnection::getDeviceState();

			// the server also sends the global state on its own, as a backup
			if(s.type != _type)
				return false;

			_state.insert(_state.end(), s.state.begin(), s.state.end());
			return true;
		}, bridgeLib::Command::DeviceState);
	}

	void DeviceConnection::handleDeviceState(baseLib::BinaryStream& _in)
	{
		// held across the reply too, it copies what was just received
		std::lock_guard lock(m_deviceStateMutex);

		TcpConnection::handleDeviceState(_in);
		onReply(bridgeLib::Command::DeviceState, _in);
	}

	void DeviceConnection::getLastDeviceState(std::vector<uint8_t>& _state)
	{
		std::lock_guard lock(m_deviceStateMutex);

		const auto& s = TcpConnection::getDeviceState().state;
		_state.insert(_state.end(), s.begin(), s.end());
	}

	bool DeviceConnection::setDeviceState(const std::vector<uint8_t>& _state, const synthLib::StateType _type)
	{
		if(_state.empty())
			return false;

		bridgeLib::DeviceState s;
		s.state = _state;
		s.type = _type;

		{
			// remembered for getLastDeviceState(), sent from our own copy as the network thread may receive into it
			std::lock_guard lock(m_deviceStateMutex);
			TcpConnection::getDeviceState() = s;
		}

		sendAwaitReply([&]
		{
			send(bridgeLib::Command::DeviceState, s);
		}, [](baseLib::BinaryStream&)
		{
			return true;
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
		}, [](baseLib::BinaryStream&)
		{
			return true;
		}, bridgeLib::Command::DeviceInfo);
	}

	void DeviceConnection::setStateFromUnknownCustomData(const std::vector<uint8_t>& _state)
	{
		sendAwaitReply([&]
		{
			bridgeLib::SetUnknownCustomData d;
			d.data = _state;
			send(bridgeLib::Command::SetUnknownCustomData, d);
		}, [](baseLib::BinaryStream&)
		{
			return true;
		}, bridgeLib::Command::DeviceInfo);
	}

	void DeviceConnection::setDspClockPercent(const uint32_t _percent)
	{
		sendAwaitReply([&]
		{
			bridgeLib::SetDspClockPercent p;
			p.percent = _percent;
			send(bridgeLib::Command::SetDspClockPercent, p);
		}, [](baseLib::BinaryStream&)
		{
			return true;
		}, bridgeLib::Command::DeviceInfo);
	}

	void DeviceConnection::onReply(const bridgeLib::Command _command, baseLib::BinaryStream& _in)
	{
		std::unique_lock lock(m_replyMutex);

		// Once only: a second message of the same kind before the caller woke up is not its reply
		if(!m_replyFunc || m_replyReceived || _command != m_replyCommand)
			return;

		// Run it while holding the lock. sendAwaitReply only clears m_replyFunc under the same lock, so
		// whatever the callback refers to is still alive for as long as we are in here.
		if(!m_replyFunc(_in))
			return;

		m_replyReceived = true;

		lock.unlock();
		m_replyCv.notify_all();
	}

	bool DeviceConnection::sendAwaitReply(const std::function<void()>& _send, const std::function<bool(baseLib::BinaryStream&)>& _reply, const bridgeLib::Command _replyCommand)
	{
		// One request at a time, see m_requestMutex. A caller that overlaps another waits here: replacing the
		// other's reply callback would make both of them time out and close the connection.
		std::lock_guard requestLock(m_requestMutex);

		// A request before this one may have timed out and closed the connection, its reply would never come
		if(m_connectionLost || !isValid())
			return false;

		std::unique_lock lock(m_replyMutex);
		m_replyFunc = _reply;
		m_replyCommand = _replyCommand;
		m_replyReceived = false;
		lock.unlock();

		// Disarm before returning: a reply that arrives after this point must not run a callback that
		// refers to our caller's stack any more.
		const auto disarm = [this]
		{
			m_replyFunc = nullptr;
			m_replyCommand = bridgeLib::Command::Invalid;
		};

		try
		{
			_send();
		}
		catch(...)
		{
			lock.lock();
			disarm();
			throw;
		}

		lock.lock();

		m_replyCv.wait_for(lock, std::chrono::seconds(g_replyTimeoutSecs), [this]
		{
			return m_replyReceived || m_connectionLost;
		});

		const auto received = m_replyReceived;

		disarm();

		lock.unlock();

		if(received)
			return true;

		if(!m_connectionLost)
		{
			LOG("Receive timeout, closing connection");
			close();
		}
		return false;
	}
}
