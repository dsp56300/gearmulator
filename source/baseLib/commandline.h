#pragma once

#include "propertyMap.h"

namespace baseLib
{
	class CommandLine : public PropertyMap
	{
	public:
		CommandLine(int _argc, char* _argv[]);
	};
}
