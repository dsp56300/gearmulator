#include "rmlRenderInterfaceShaders.h"

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
			{{"USE_TRANSFORMATION_MATRIX", "USE_COLOR_MATRIX", "USE_TEXTURE"}}    // FullscreenColorMatrix
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
}
