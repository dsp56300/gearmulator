#pragma once

#include "baseLib/binarystream.h"

namespace pluginLib
{
	using PluginStream = baseLib::BinaryStream;

	enum ParameterLinkType : uint32_t
	{
		None = 0,
		Source = 1,
		Target = 2
	};

	using ParamValue = int32_t;

	enum class DeviceType
	{
		Local,
		Remote,
		Dummy
	};
}
