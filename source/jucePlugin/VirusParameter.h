#pragma once

#include "../jucePluginLib/parameter.h"

namespace Virus
{
	class Controller;

	class Parameter : public pluginLib::Parameter
	{
    public:
		Parameter(pluginLib::Controller& _controller, const pluginLib::Description& _desc, uint8_t _partNum, int _uniqueId);
		float getDefaultValue() const override;
	};
} // namespace Virus
