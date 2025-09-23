#pragma once

#include "rmlRendererGL2Types.h"
#include "rmlShader.h"

namespace juceRmlUi::gl2
{
	class RenderInterfaceShaders
	{
	public:
		CompiledShader* create(const Rml::String& _name, const Rml::Dictionary& _parameters);

		RmlShader& getShader(Rml::CoreInstance& _coreInstance, ShaderType _type);

		RmlShader* getBlurShader(Rml::CoreInstance& _coreInstance, uint32_t& _passCount, float _sigma);

		CompiledShader* createBlurShader(Rml::CoreInstance& _coreInstance, float _sigma);

	private:
		static std::vector<float> generateGaussianKernelWeights(float _sigma);
		static std::string generateBlurCode(float _sigma);

		std::unordered_map<ShaderType, RmlShader> m_shaders;
		std::unordered_map<int, std::unique_ptr<RmlShader>> m_blurShaders;
	};
}
