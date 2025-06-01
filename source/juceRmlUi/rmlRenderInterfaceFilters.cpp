#include "rmlRenderInterfaceFilters.h"

#include "rmlRenderInterfaceShaders.h"

namespace juceRmlUi
{
	RenderInterfaceFilters::RenderInterfaceFilters(RenderInterfaceShaders& _shaders) : m_shaders(_shaders)
	{
	}

	uint32_t RenderInterfaceFilters::create(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		return m_shaders.create(_name, _parameters);
	}
}
