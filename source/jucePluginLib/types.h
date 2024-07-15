#pragma once

#include "synthLib/binarystream.h"

namespace pluginLib
{
	using PluginStream = synthLib::BinaryStream;

	enum ParameterLinkType : uint32_t
	{
		None = 0,
		Source = 1,
		Target = 2
	};
}
