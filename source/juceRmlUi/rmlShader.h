#pragma once

#include <cstdint>
#include <unordered_map>

#include "juce_opengl/opengl/juce_gl.h"
#include "RmlUi/Core/Types.h"

namespace juceRmlUi
{
	class RmlShader
	{
	public:
		enum class AttribType : uint8_t
		{
			Position,
			TexCoord,
			VertexColor,
			Texture,
			ColorMatrix,

			Count
		};

		struct ShaderParams
		{
			uint32_t texture = 0;
			Rml::Matrix4f colorMatrix = Rml::Matrix4f::Identity();
		};

		explicit RmlShader(uint32_t _programId);

		uint32_t getProgramId() const noexcept { return m_programId; }

		bool hasAttribute(AttribType _type) const noexcept;
		bool hasUniform(AttribType _type) const noexcept;

		static void setUniformMatrix(int _location, const Rml::Matrix4f& _matrix);

		void enableShader(const ShaderParams& _params);
		void disableShader();

	private:
		uint32_t m_programId;
		std::unordered_map<AttribType, uint32_t> m_vertexAttribLocations;
		std::unordered_map<AttribType, uint32_t> m_uniformLocations;
	};
}
