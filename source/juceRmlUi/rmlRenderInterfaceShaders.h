#pragma once

#include "rmlRendererGL2Types.h"
#include "rmlShader.h"

namespace juceRmlUi::gl2
{
	class RenderInterfaceShaders
	{
	public:
		CompiledShader* create(const Rml::String& _name, const Rml::Dictionary& _parameters);

		RmlShader& getShader(ShaderType _type);

	private:
		std::unordered_map<ShaderType, RmlShader> m_shaders;
	};
}
