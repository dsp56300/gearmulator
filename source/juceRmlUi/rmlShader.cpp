#include "rmlShader.h"

#include <cassert>
#include <iterator>

#include "rmlRendererGL2.h"

#include "juce_opengl/opengl/juce_gl.h"

#include "RmlUi/Core/Types.h"

namespace juceRmlUi::gl2
{
	namespace
	{
		constexpr const char* g_vertexAttribNames[] =
		{
			"aPos",
			"aTexCoord",
			"aVertexColor",
			"uTexture",
			"uColorMatrix"
		};

		static_assert(std::size(g_vertexAttribNames) == static_cast<uint32_t>(RmlShader::AttribType::Count));
	}

	RmlShader::RmlShader(const uint32_t _programId) : m_programId(_programId)
	{
		using namespace juce::gl;

		for (uint32_t i=0; i<static_cast<uint32_t>(AttribType::Count); ++i)
		{
			const auto attrib = static_cast<AttribType>(i);
			const auto& attribName = g_vertexAttribNames[i];

			switch (attribName[0])
			{
			case 'a': // attribute
				{
					const auto attribLocation = glGetAttribLocation(m_programId, attribName);
					CHECK_OPENGL_ERROR;
					if (attribLocation >= 0)
						m_vertexAttribLocations.insert({attrib, attribLocation});
				}
				break;
			case 'u': // uniform
				{
					const auto uniformLocation = glGetUniformLocation(m_programId, attribName);
					CHECK_OPENGL_ERROR;

					if (uniformLocation >= 0)
						m_uniformLocations.insert({attrib, static_cast<uint32_t>(uniformLocation)});
				}
				break;
			default:
				assert(false && "invalid attribute name");
			}
		}
	}

	bool RmlShader::hasAttribute(const AttribType _type) const noexcept
	{
		return m_vertexAttribLocations.find(_type) != m_vertexAttribLocations.end();
	}

	bool RmlShader::hasUniform(const AttribType _type) const noexcept
	{
		return m_uniformLocations.find(_type) != m_uniformLocations.end();
	}

	void RmlShader::setUniformMatrix(int _location, const Rml::Matrix4f& _matrix)
	{
		constexpr bool transpose = std::is_same_v<decltype(_matrix), Rml::RowMajorMatrix4f>;
		juce::gl::glUniformMatrix4fv(_location, 1, transpose, _matrix.data());
		CHECK_OPENGL_ERROR;
	}

	void RmlShader::enableShader(const ShaderParams& _params)
	{
		using namespace juce::gl;

		glUseProgram(m_programId);
		CHECK_OPENGL_ERROR;

		for (const auto& [attrib, location] : m_vertexAttribLocations)
		{
			glEnableVertexAttribArray(location);
			CHECK_OPENGL_ERROR;
		}

		for (const auto& [uniform, location] : m_uniformLocations)
		{
			const auto loc = static_cast<int>(location);
			switch (uniform)
			{
			case AttribType::Texture:
				glActiveTexture(GL_TEXTURE0);
				CHECK_OPENGL_ERROR;
				glBindTexture(GL_TEXTURE_2D, _params.texture);
				CHECK_OPENGL_ERROR;
				glUniform1i(loc, 0); // texture unit 0
				CHECK_OPENGL_ERROR;
				break;
			case AttribType::ColorMatrix:
				setUniformMatrix(loc, _params.colorMatrix);
				CHECK_OPENGL_ERROR;
				break;
			default:
				break;
			}
		}
	}

	void RmlShader::disableShader()
	{
		using namespace juce::gl;

		glUseProgram(0);
		CHECK_OPENGL_ERROR;

		for (const auto& [attrib, location] : m_vertexAttribLocations)
		{
			glDisableVertexAttribArray(location);
			CHECK_OPENGL_ERROR;
		}
	}

	void RmlShader::setupVertexAttributes()
	{
		using namespace juce::gl;

		auto itPos = m_vertexAttribLocations.find(AttribType::Position);
		if (itPos != m_vertexAttribLocations.end())
		{
			glVertexAttribPointer(
			    itPos->second,                                             // Attribute location
			    2,                                                         // Number of components (x, y, z)
			    GL_FLOAT,                                                  // Type
			    GL_FALSE,                                                  // Normalize?
			    sizeof(Rml::Vertex),                                       // Stride
			    reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, position)) // Offset to position data  // NOLINT(performance-no-int-to-ptr)
			);
			CHECK_OPENGL_ERROR;
		}

		auto itColor = m_vertexAttribLocations.find(AttribType::VertexColor);
		if (itColor != m_vertexAttribLocations.end())
		{
			glVertexAttribPointer(
				itColor->second,                                            // Attribute location
				4,                                                         // Number of components (r, g, b, a)
				GL_UNSIGNED_BYTE,                                          // Type
				GL_TRUE,                                                   // Normalize?
				sizeof(Rml::Vertex),                                       // Stride
				reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, colour))   // Offset to color data  // NOLINT(performance-no-int-to-ptr)
			);
			CHECK_OPENGL_ERROR;
		}

		auto itTex = m_vertexAttribLocations.find(AttribType::TexCoord);
		if (itTex != m_vertexAttribLocations.end())
		{
			glVertexAttribPointer(
			    itTex->second,                                              // Attribute location
			    2,                                                          // Number of components (x, y)
			    GL_FLOAT,                                                   // Type
			    GL_FALSE,                                                   // Normalize?
			    sizeof(Rml::Vertex),                                        // Stride
			    reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, tex_coord)) // Offset to UV data // NOLINT(performance-no-int-to-ptr)
			);
		}
	}
}
