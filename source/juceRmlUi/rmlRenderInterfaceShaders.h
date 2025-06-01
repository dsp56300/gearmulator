#pragma once

#include "RmlUi/Core/Variant.h"

namespace juceRmlUi
{
	class RenderInterfaceShaders
	{
	public:
		uint32_t create(const Rml::String& _name, const Rml::Dictionary& _parameters);
	};
}
