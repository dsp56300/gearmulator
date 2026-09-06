#include "tcpConnection.h"

#include "audioBuffers.h"
#include "networkLib/exception.h"
#include "networkLib/logging.h"

#include "synthLib/midiTypes.h"

namespace bridgeLib
{
	TcpConnection::TcpConnection(std::unique_ptr<networkLib::TcpStream>&& _stream) : CommandReader(nullptr), m_stream(std::move(_stream))
	{
		m_audioTransferBuffer.reserve(16384);

		start();
	}

	TcpConnection::~TcpConnection()
	{
		shutdown();
	}

	void TcpConnection::handleCommand(const Command _command, baseLib::BinaryStream& _in)
	{
		switch (_command)
		{
		case Command::Invalid:
		case Command::Ping:
		case Command::Pong:
			break;
		case Command::PluginInfo:			handleStruct<PluginDesc>(_in); break;
		case Command::ServerInfo:			handleStruct<ServerInfo>(_in); break;
		case Command::Error:				handleStruct<Error>(_in); break;
		case Command::DeviceInfo:			handleDeviceInfo(_in); break;
		case Command::Midi:					handleMidi(_in); break;
		case Command::Audio:				handleAudio(_in); break;
		case Command::DeviceState:			handleDeviceState(_in); break;
		case Command::RequestDeviceState:	handleRequestDeviceState(_in); break;
		case Command::DeviceCreateParams:	handleStruct<DeviceCreateParams>(_in); break;
		case Command::SetSamplerate:		handleStruct<SetSamplerate>(_in); break;
		case Command::SetDspClockPercent:	handleStruct<SetDspClockPercent>(_in); break;
		case Command::SetUnknownCustomData:	handleStruct<SetUnknownCustomData>(_in); break;
		}
	}

	void TcpConnection::threadFunc()
	{
		try
		{
			NetworkThread::threadFunc();
		}
		catch (const networkLib::NetException& e)
		{
			m_stream->close();
			LOGNET(networkLib::LogLevel::Warning, "Network Exception, code " << e.type() << ": " << e.what());
			handleException(e);
		}
	}

	void TcpConnection::threadLoopFunc()
	{
		read(*m_stream);
	}

	void TcpConnection::send(const Command _command, const CommandStruct& _data)
	{
		m_writer.build(_command, _data);
		m_writer.write(*m_stream);
	}

	void TcpConnection::send(Command _command)
	{
		m_writer.build(_command);
		m_writer.write(*m_stream);
	}

	void TcpConnection::handleMidi(baseLib::BinaryStream& _in)
	{
		synthLib::SMidiEvent& ev = m_midiEvent;
		_in.read(ev.a);
		_in.read(ev.b);
		_in.read(ev.c);
		_in.read(ev.sysex);
		_in.read(ev.offset);
		ev.source = static_cast<synthLib::MidiEventSource>(_in.read<uint8_t>());
		handleMidi(ev);
	}

	void TcpConnection::sendAudio(const float* const* _data, const uint32_t _numChannels, const uint32_t _numSamplesPerChannel)
	{
		auto& s = m_writer.build(Command::Audio);
		s.write(static_cast<uint8_t>(_numChannels));
		s.write(_numSamplesPerChannel);

		for(uint32_t i=0; i<_numChannels; ++i)
		{
			// if a channel has data, write it. If not, write 0 for the length
			if(const float* channelData = _data[i])
			{
				s.write(_numSamplesPerChannel);
				s.write(channelData, _numSamplesPerChannel);
			}
			else
			{
				s.write(0);
			}
		}
		send();
	}

	void TcpConnection::sendAudio(AudioBuffers& _buffers, const uint32_t _numChannels, uint32_t _numSamplesPerChannel)
	{
		auto& s = m_writer.build(Command::Audio);
		s.write(static_cast<uint8_t>(_numChannels));
		s.write(_numSamplesPerChannel);

		if(m_audioTransferBuffer.size() < _numSamplesPerChannel)
			m_audioTransferBuffer.resize(_numSamplesPerChannel);

		for(uint32_t i=0; i<_numChannels; ++i)
		{
			_buffers.readInput(i, m_audioTransferBuffer, _numSamplesPerChannel);
			s.write(_numSamplesPerChannel);
			s.write(m_audioTransferBuffer.data(), _numSamplesPerChannel);
		}

		_buffers.onInputRead(_numSamplesPerChannel);

		send();
	}

	uint32_t TcpConnection::handleAudio(float* const* _output, baseLib::BinaryStream& _in)
	{
		const uint32_t numChannels = _in.read<uint8_t>();
		const uint32_t numSamplesMax = _in.read<uint32_t>();

		for(uint32_t i=0; i<numChannels; ++i)
		{
			const auto numSamples = _in.read<uint32_t>();
			if(numSamples)
			{
				assert(_output[i]);
				_in.read(_output[i], numSamples);
			}
		}
		return numSamplesMax;
	}

	void TcpConnection::handleAudio(AudioBuffers& _buffers, baseLib::BinaryStream& _in)
	{
		const uint32_t numChannels = _in.read<uint8_t>();
		const uint32_t numSamplesMax = _in.read<uint32_t>();

		for(uint32_t i=0; i<numChannels; ++i)
		{
			const auto numSamples = _in.read<uint32_t>();

			if(numSamples)
			{
				if(m_audioTransferBuffer.size() < numSamples)
					m_audioTransferBuffer.resize(numSamples);
				_in.read(m_audioTransferBuffer.data(), numSamples);
				_buffers.writeOutput(i, m_audioTransferBuffer, numSamples);
			}
		}
		_buffers.onOutputWritten(numSamplesMax);
	}

	void TcpConnection::handleAudio(baseLib::BinaryStream& _in)
	{
	}

	void TcpConnection::handleRequestDeviceState(baseLib::BinaryStream& _in)
	{
		RequestDeviceState requestDeviceState;
		requestDeviceState.read(_in);
		handleRequestDeviceState(requestDeviceState);
	}

	void TcpConnection::handleDeviceState(baseLib::BinaryStream& _in)
	{
		m_deviceState.read(_in);
		handleDeviceState(m_deviceState);
	}

	void TcpConnection::handleDeviceInfo(baseLib::BinaryStream& _in)
	{
		handleStruct<DeviceDesc>(_in);
	}

	bool TcpConnection::send(const synthLib::SMidiEvent& _ev)
	{
		if(!isValid())
			return false;
		auto& bs = m_writer.build(Command::Midi);
		bs.write(_ev.a);
		bs.write(_ev.b);
		bs.write(_ev.c);
		bs.write(_ev.sysex);
		bs.write(_ev.offset);
		bs.write<uint8_t>(static_cast<uint8_t>(_ev.source));
		send();
		return true;
	}

	void TcpConnection::close() const
	{
		if(m_stream)
			m_stream->close();
	}

	void TcpConnection::shutdown()
	{
		close();
		stop();
		m_stream.reset();
	}
}
