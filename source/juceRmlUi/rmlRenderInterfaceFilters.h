#pragma once

#include "RmlUi/Core/Variant.h"

namespace juceRmlUi
{
	class RenderInterfaceShaders;

	class RenderInterfaceFilters
	{
	public:
		explicit RenderInterfaceFilters(RenderInterfaceShaders& _shaders);
		uint32_t create(const Rml::String& _name, const Rml::Dictionary& _parameters);

	private:
		RenderInterfaceShaders& m_shaders;
	};
}
