#include "rmlRenderInterfaceShaders.h"

#include "juce_opengl/opengl/juce_gl.h"

namespace juceRmlUi
{
	using namespace juce::gl;

	namespace
	{
		const char* g_vertexShader = R"(
			// vertex_shader.glsl
			#version 110
			attribute vec3 aPos;
			attribute vec2 aTexCoord;
			varying vec2 vTexCoord;
			void main() {
			    gl_Position = vec4(aPos, 1.0);
			    vTexCoord = aTexCoord;
			})";

		const char* g_fragmentShader = R"(
			// fragment_shader.glsl
			#version 110
			uniform sampler2D uTexture;
			varying vec2 vTexCoord;
			void main() {
			    vec4 color = texture2D(uTexture, vTexCoord);
				float gray = dot(color.rgb, vec3(0.299, 0.587, 0.114));
			    color.rgb = vec3(gray, gray, gray);
			    gl_FragColor = color;
			})";		

		GLuint compileShader(const GLenum _type, const char* _source)
		{
			// Compile shader and check for errors
			GLuint shader = glCreateShader(_type);
			glShaderSource(shader, 1, &_source, nullptr);
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
		};
		GLuint createProgram(const char* _vertexSource, const char* _fragmentSource)
		{
		    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, _vertexSource);
		    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, _fragmentSource);
		    GLuint program = glCreateProgram();
		    glAttachShader(program, vertexShader);
		    glAttachShader(program, fragmentShader);
		    glLinkProgram(program);
		    glDeleteShader(vertexShader);
		    glDeleteShader(fragmentShader);
		    return program;
		};
	}

	uint32_t RenderInterfaceShaders::create(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		const auto prog = createProgram(g_vertexShader, g_fragmentShader);
		return prog;
	}
}
