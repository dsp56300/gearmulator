#pragma once

#include "RmlUi/Core/Types.h"

namespace juceRmlUi
{
	namespace gl2
	{
		enum class ShaderType : uint8_t
		{
			DefaultTextured,
			DefaultColored,

			FullscreenColorMatrix,
			Fullscreen,

			Count
		};

		struct ShaderParams
		{
			uint32_t texture = 0;
			Rml::Matrix4f colorMatrix = Rml::Matrix4f::Identity();
		};

		struct CompiledGeometry
		{
			uint32_t vertexBuffer = 0;
			uint32_t indexBuffer = 0;
			size_t indexCount = 0;
		};

		struct LayerHandleData
		{
			uint32_t framebuffer = 0;
			uint32_t texture = 0;
		};

		struct CompiledShader
		{
			ShaderType type;
			ShaderParams params;
		};
	}
}
