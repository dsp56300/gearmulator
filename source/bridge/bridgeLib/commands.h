#pragma once

#include <cstdint>

#include "command.h"
#include "commandStruct.h"
#include "error.h"
#include "types.h"
#include "baseLib/md5.h"
#include "synthLib/device.h"
#include "synthLib/deviceTypes.h"

namespace bridgeLib
{
	enum class ErrorCode : uint32_t;

	enum class Command : uint32_t
	{
		Invalid = 0,

		Ping = cmd("ping"),
		Pong = cmd("pong"),

		PluginInfo = cmd("PInf"),
		ServerInfo = cmd("SInf"),

		Error = cmd("Erro"),

		DeviceInfo = cmd("DevI"),

		DeviceCreateParams = cmd("DCrP"),
		RequestRom = cmd("ROMr"),

		Midi = cmd("MIDI"),
		Audio = cmd("Wave"),

		DeviceState = cmd("DvSt"),
		RequestDeviceState = cmd("RqDS"),

		SetSamplerate = cmd("SmpR"),

		SetUnknownCustomData = cmd("UnkD"),

		SetDspClockPercent = cmd("DspC")
	};

	std::string commandToString(Command _command);
	void commandToBuffer(std::array<char,5>& _buffer, Command _command);

	struct Error : CommandStruct
	{
		ErrorCode code = ErrorCode::Ok;
		std::string msg;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};

	struct ServerInfo : CommandStruct
	{
		uint32_t protocolVersion;
		uint32_t portUdp;
		uint32_t portTcp;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};

	struct PluginDesc : CommandStruct
	{
		uint32_t protocolVersion = g_protocolVersion;
		std::string pluginName;
		uint32_t pluginVersion = 0;
		std::string plugin4CC;
		SessionId sessionId = 0;

		PluginDesc()
		{
			pluginName.reserve(32);
			plugin4CC.reserve(32);
		}

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;

		bool operator == (const PluginDesc& _desc) const
		{
			return
				pluginVersion == _desc.pluginVersion && 
				plugin4CC == _desc.plugin4CC &&
				pluginName == _desc.pluginName &&
				protocolVersion == _desc.protocolVersion;
		}

		bool operator < (const PluginDesc& _desc) const
		{
			if(pluginVersion < _desc.pluginVersion) return true;
			if(pluginVersion > _desc.pluginVersion) return false;
			if(plugin4CC < _desc.plugin4CC) return true;
			if(plugin4CC > _desc.plugin4CC) return false;
			if(pluginName < _desc.pluginName) return true;
			if(pluginName > _desc.pluginName) return false;
			if(protocolVersion < _desc.protocolVersion) return true;
			return false;
		}
	};

	struct DeviceCreateParams : CommandStruct
	{
		synthLib::DeviceCreateParams params;

		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
	};

	struct DeviceDesc : CommandStruct
	{
		float samplerate = 0.0f;
		uint32_t outChannels = 0;
		uint32_t inChannels = 0;
		uint32_t dspClockPercent = 0;
		uint64_t dspClockHz = 0;
		uint32_t latencyInToOut = 0;
		uint32_t latencyMidiToOut = 0;
		std::vector<float> preferredSamplerates;
		std::vector<float> supportedSamplerates;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};

	struct RequestDeviceState : CommandStruct
	{
		synthLib::StateType type;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};

	struct DeviceState : CommandStruct
	{
		synthLib::StateType type;
		std::vector<uint8_t> state;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;

		bool isValid() const { return !state.empty(); }
	};

	struct SetSamplerate : CommandStruct
	{
		float samplerate;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};

	struct SetUnknownCustomData : CommandStruct
	{
		std::vector<uint8_t> data;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};

	struct SetDspClockPercent : CommandStruct
	{
		uint32_t percent;

		baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const override;
		baseLib::BinaryStream& read(baseLib::BinaryStream& _s) override;
	};
}
