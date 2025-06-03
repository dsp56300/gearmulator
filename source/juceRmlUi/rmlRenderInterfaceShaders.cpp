#include "rmlRenderInterfaceShaders.h"

#include <cassert>
#include <sstream>

#include "juce_opengl/opengl/juce_gl.h"

#include "rmlShaders.h"

#include "RmlUi/Core/Log.h"

namespace juceRmlUi::gl2
{
	using namespace juce::gl;

	namespace
	{
		struct ShaderSetup
		{
			std::vector<std::string> defines;
		};

		const ShaderSetup g_shaderSetups[] =
		{
			{{"USE_TRANSFORMATION_MATRIX", "USE_VERTEX_COLOR", "USE_TEXTURE"}},   // DefaultTextured
			{{"USE_TRANSFORMATION_MATRIX", "USE_VERTEX_COLOR"}},                  // DefaultColored
			{{"USE_COLOR_MATRIX", "USE_TEXTURE"}},                                // FullscreenColorMatrix
			{{"USE_TEXTURE"}},                                                    // Fullscreen
			{{"USE_BLUR"}}                                                        // Blur
		};

		static_assert(std::size(g_shaderSetups) == static_cast<uint32_t>(ShaderType::Count));

		const auto g_shaderHeader = "#version 110\n";

		GLuint compileShader(const GLenum _type, const char* _source, const std::vector<std::string>& _defines)
		{
			auto source = std::string(g_shaderHeader);

			for (const auto& define : _defines)
				source += "#define " + define + '\n';

			source += _source;

			const char* sourcePtr = source.c_str();

			// Compile shader and check for errors
			GLuint shader = glCreateShader(_type);
			glShaderSource(shader, 1, &sourcePtr, nullptr);
			glCompileShader(shader);
			GLint success;
			glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
			if (!success)
			{
				char infoLog[512];
				glGetShaderInfoLog(shader, 512, nullptr, infoLog);
				Rml::Log::Message(Rml::Log::LT_ERROR, "Shader compilation failed:\n%s", infoLog);
			}
			return shader;
		}

		GLuint createProgram(const char* _vertexSource, const char* _fragmentSource, const std::vector<std::string>& _defines)
		{
		    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, _vertexSource, _defines);
		    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, _fragmentSource, _defines);
		    GLuint program = glCreateProgram();
		    glAttachShader(program, vertexShader);
		    glAttachShader(program, fragmentShader);
		    glLinkProgram(program);
		    glDeleteShader(vertexShader);
		    glDeleteShader(fragmentShader);
		    return program;
		}

		int sigmaToRadius(const float _sigma)
		{
			return static_cast<int>(std::ceil(3.0f * _sigma));
		}

		float radiusToSigma(const int _radius)
		{
			return static_cast<float>(_radius) / 3.0f;
		}

		void calculateBlurParameters(uint32_t& _passCount, float& _sigmaPerPass, const float _sigmaTarget)
		{
			constexpr auto maxRadius = 9.0f; // shader will have to do maxRadius * 2 + 1 texture fetches
			constexpr auto sigmaPerPass = maxRadius / 3.0f;

			if (_sigmaTarget < sigmaPerPass)
			{
				_passCount = 1;
				_sigmaPerPass = _sigmaTarget;
				return;
			}
			_passCount = static_cast<uint32_t>(std::ceil((_sigmaTarget * _sigmaTarget) / (sigmaPerPass * sigmaPerPass)));
			_sigmaPerPass = _sigmaTarget / static_cast<float>(std::sqrt(_passCount));
		}
	}

	CompiledShader* RenderInterfaceShaders::create(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
//		const auto prog = createProgram(g_vertexShader, g_fragmentShader ,{"USE_TEXTURE", "USE_TRANSFORMATION_MATRIX"});
//		return prog;
		return nullptr;
	}

	RmlShader& RenderInterfaceShaders::getShader(ShaderType _type)
	{
		auto it = m_shaders.find(_type);
		if (it != m_shaders.end())
			return it->second;

		const auto& setup = g_shaderSetups[static_cast<uint32_t>(_type)];

		auto prog = createProgram(g_vertexShader, g_fragmentShader, setup.defines);

		m_shaders.emplace(_type, RmlShader{prog});

		return m_shaders.at(_type);
	}

	RmlShader* RenderInterfaceShaders::getBlurShader(uint32_t& _passCount, const float _sigma)
	{
		float sigmaPerPass;

		calculateBlurParameters(_passCount, sigmaPerPass, _sigma);

		const auto sigmaInt = static_cast<int>(sigmaPerPass * 10.0f);
		auto existing = m_blurShaders.find(sigmaInt);
		if (existing != m_blurShaders.end())
			return existing->second.get();

		const auto& setup = g_shaderSetups[static_cast<uint32_t>(ShaderType::Blur)];

		const auto blurCode = generateBlurCode(sigmaPerPass);

		std::string fsCode = g_fragmentShader;

		constexpr const char* const key = "BLUR_CODE"; // placeholder for the blur code in the fragment shader
		auto pos = fsCode.find(key);
		if (pos != std::string::npos)
		{
			fsCode.replace(pos, strlen(key), blurCode);
		}
		else
		{
			assert(false && "Blur code placeholder not found in fragment shader code");
		}

		const auto prog = createProgram(g_vertexShader, fsCode.c_str(), setup.defines);

		auto shader = std::make_unique<RmlShader>(prog);

		auto* result = shader.get();

		m_blurShaders.emplace(sigmaInt, std::move(shader));

		return result;
	}

	CompiledShader* RenderInterfaceShaders::createBlurShader(const float _sigma)
	{
		uint32_t passCount;
		RmlShader* shader = getBlurShader(passCount, _sigma);
		if (!shader)
			return nullptr;
		auto* compiledShader = new CompiledShader();

		compiledShader->type = ShaderType::Blur;
		compiledShader->shader = shader;
		compiledShader->params.blurPasses = passCount;

		return compiledShader;
	}

	std::vector<float> RenderInterfaceShaders::generateGaussianKernelWeights(const float _sigma)
	{
	    const int radius = sigmaToRadius(_sigma);
	    const int size = radius * 2 + 1;

		std::vector<float> weights;
		weights.reserve(size);

	    float sigmaQ = _sigma * _sigma;
	    float sum = 0.0f;

	    for (int i = -radius; i <= radius; ++i)
	    {
	        float weight = std::exp(-static_cast<float>(i * i) / (2.0f * sigmaQ));
	        weights.push_back(weight);
	        sum += weight;
	    }

	    for (auto& w : weights)
	        w /= sum;

	    return weights;
	}

	std::string RenderInterfaceShaders::generateBlurCode(const float _sigma)
	{
		auto weights = generateGaussianKernelWeights(_sigma);
		auto radius = sigmaToRadius(_sigma);

		std::stringstream code;

		for (auto i=-radius; i<=radius; ++i)
		{
			//	sum += texture2D(uTexture, vTexCoord + vec2(offset,offset) * uBlurScale) * weight;

			code << "sum += texture2D(uTexture, vTexCoord";

			if (i)
			{
				code << " + vec2(" << i << ", " << i << ") * uBlurScale";
			}
			code << ") * " << std::to_string(weights[i + radius]) << ";\n";
		}

		const auto c = code.str();
		return c;
	}
}
