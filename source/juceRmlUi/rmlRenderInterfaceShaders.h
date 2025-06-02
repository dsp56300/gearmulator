#pragma once

#include "RmlUi/Core/Variant.h"

namespace juceRmlUi
{
	class RenderInterfaceShaders
	{
	public:
		enum class ShaderType
		{
			DefaultTextured,
			DefaultColored,

			FullscreenColorMatrix,

			Count
		};

		uint32_t create(const Rml::String& _name, const Rml::Dictionary& _parameters);
	};
}
