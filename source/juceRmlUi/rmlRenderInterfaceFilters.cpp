#include "rmlRenderInterfaceFilters.h"

#include "rmlRenderInterfaceShaders.h"

namespace juceRmlUi::gl2
{
	RenderInterfaceFilters::RenderInterfaceFilters(RenderInterfaceShaders& _shaders) : m_shaders(_shaders)
	{
	}

	CompiledShader* RenderInterfaceFilters::create(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		return m_shaders.create(_name, _parameters);
	}
}
