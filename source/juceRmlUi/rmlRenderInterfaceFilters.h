#pragma once

#include "RmlUi/Core/Variant.h"

namespace juceRmlUi::gl2
{
	struct CompiledShader;
	class RenderInterfaceShaders;

	class RenderInterfaceFilters
	{
	public:
		explicit RenderInterfaceFilters(RenderInterfaceShaders& _shaders);
		CompiledShader* create(Rml::CoreInstance& _coreInstance, const Rml::String& _name, const Rml::Dictionary& _parameters);

	private:
		RenderInterfaceShaders& m_shaders;
	};
}
