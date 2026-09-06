#include "commands.h"

#include <array>

#include "baseLib/binarystream.h"

namespace bridgeLib
{
	std::string commandToString(const Command _command)
	{
		std::array<char, 5> temp;
		commandToBuffer(temp, _command);
		return {temp.data()};
	}

	void commandToBuffer(std::array<char, 5>& _buffer, Command _command)
	{
		const auto c = static_cast<uint32_t>(_command);
		_buffer[0] = static_cast<char>((c >> 24) & 0xff);
		_buffer[1] = static_cast<char>((c >> 16) & 0xff);
		_buffer[2] = static_cast<char>((c >> 8) & 0xff);
		_buffer[3] = static_cast<char>((c) & 0xff);
		_buffer[4] = 0;
	}

	baseLib::BinaryStream& Error::write(baseLib::BinaryStream& _s) const
	{
		_s.write<uint32_t>(static_cast<uint32_t>(code));
		_s.write(msg);
		return _s;
	}

	baseLib::BinaryStream& Error::read(baseLib::BinaryStream& _s)
	{
		code = static_cast<ErrorCode>(_s.read<uint32_t>());
		msg = _s.readString();
		return _s;
	}

	baseLib::BinaryStream& ServerInfo::write(baseLib::BinaryStream& _s) const
	{
		_s.write(protocolVersion);
		_s.write(portUdp);
		_s.write(portTcp);
		return _s;
	}

	baseLib::BinaryStream& ServerInfo::read(baseLib::BinaryStream& _s)
	{
		_s.read(protocolVersion);
		_s.read(portUdp);
		_s.read(portTcp);
		return _s;
	}

	baseLib::BinaryStream& PluginDesc::write(baseLib::BinaryStream& _s) const
	{
		_s.write(protocolVersion);
		_s.write(pluginName);
		_s.write(pluginVersion);
		_s.write(plugin4CC);
		_s.write(sessionId);
		return _s;
	}

	baseLib::BinaryStream& PluginDesc::read(baseLib::BinaryStream& _s)
	{
		_s.read(protocolVersion);
		pluginName = _s.readString();
		_s.read(pluginVersion);
		plugin4CC = _s.readString();
		_s.read(sessionId);
		return _s;
	}

	baseLib::BinaryStream& DeviceCreateParams::read(baseLib::BinaryStream& _s)
	{
		_s.read(params.preferredSamplerate);
		_s.read(params.hostSamplerate);
		params.romName = _s.readString();
		_s.read(params.romData);
		_s.read(params.romHash);
		_s.read(params.customData);
		return _s;
	}

	baseLib::BinaryStream& DeviceCreateParams::write(baseLib::BinaryStream& _s) const
	{
		_s.write(params.preferredSamplerate);
		_s.write(params.hostSamplerate);
		_s.write(params.romName);
		_s.write(params.romData);
		_s.write(params.romHash);
		_s.write(params.customData);
		return _s;
	}

	baseLib::BinaryStream& DeviceDesc::write(baseLib::BinaryStream& _s) const
	{
		_s.write(samplerate);
		_s.write(outChannels);
		_s.write(inChannels);
		_s.write(dspClockPercent);
		_s.write(dspClockHz);
		_s.write(latencyInToOut);
		_s.write(latencyMidiToOut);
		_s.write(preferredSamplerates);
		_s.write(supportedSamplerates);
		return _s;
	}

	baseLib::BinaryStream& DeviceDesc::read(baseLib::BinaryStream& _s)
	{
		_s.read(samplerate);
		_s.read(outChannels);
		_s.read(inChannels);
		_s.read(dspClockPercent);
		_s.read(dspClockHz);
		_s.read(latencyInToOut);
		_s.read(latencyMidiToOut);
		_s.read(preferredSamplerates);
		_s.read(supportedSamplerates);
		return _s;
	}

	baseLib::BinaryStream& RequestDeviceState::write(baseLib::BinaryStream& _s) const
	{
		_s.write(static_cast<uint32_t>(type));
		return _s;
	}

	baseLib::BinaryStream& RequestDeviceState::read(baseLib::BinaryStream& _s)
	{
		type = static_cast<synthLib::StateType>(_s.read<uint32_t>());
		return _s;
	}

	baseLib::BinaryStream& DeviceState::write(baseLib::BinaryStream& _s) const
	{
		_s.write(static_cast<uint32_t>(type));
		_s.write(state);
		return _s;
	}

	baseLib::BinaryStream& DeviceState::read(baseLib::BinaryStream& _s)
	{
		type = static_cast<synthLib::StateType>(_s.read<uint32_t>());
		_s.read(state);
		return _s;
	}

	baseLib::BinaryStream& SetSamplerate::write(baseLib::BinaryStream& _s) const
	{
		_s.write(samplerate);
		return _s;
	}

	baseLib::BinaryStream& SetSamplerate::read(baseLib::BinaryStream& _s)
	{
		_s.read(samplerate);
		return _s;
	}

	baseLib::BinaryStream& SetUnknownCustomData::write(baseLib::BinaryStream& _s) const
	{
		_s.write(data);
		return _s;
	}

	baseLib::BinaryStream& SetUnknownCustomData::read(baseLib::BinaryStream& _s)
	{
		_s.read(data);
		return _s;
	}

	baseLib::BinaryStream& SetDspClockPercent::write(baseLib::BinaryStream& _s) const
	{
		_s.write(percent);
		return _s;
	}

	baseLib::BinaryStream& SetDspClockPercent::read(baseLib::BinaryStream& _s)
	{
		_s.read(percent);
		return _s;
	}
}
