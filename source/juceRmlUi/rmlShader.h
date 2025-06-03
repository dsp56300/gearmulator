#pragma once

#include <cstdint>
#include <unordered_map>

#include "RmlUi/Core/Types.h"

namespace juceRmlUi::gl2
{
	struct ShaderParams;

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
			BlurScale,

			Count
		};

		explicit RmlShader(uint32_t _programId);

		RmlShader(RmlShader&&) noexcept = default;
		RmlShader(const RmlShader&) = delete;

		RmlShader& operator=(RmlShader&&) noexcept = default;
		RmlShader& operator=(const RmlShader&) = delete;

		uint32_t getProgramId() const noexcept { return m_programId; }

		bool hasAttribute(AttribType _type) const noexcept;
		bool hasUniform(AttribType _type) const noexcept;

		static void setUniformMatrix(int _location, const Rml::Matrix4f& _matrix);

		void enableShader(const gl2::ShaderParams& _params);
		void disableShader();

		void setupVertexAttributes();

		bool isValid() const noexcept { return m_programId != 0; }

	private:
		uint32_t m_programId;

		std::unordered_map<AttribType, uint32_t> m_vertexAttribLocations;
		std::unordered_map<AttribType, uint32_t> m_uniformLocations;
	};
}
