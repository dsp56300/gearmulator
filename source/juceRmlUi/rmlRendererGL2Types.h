#pragma once

#include "RmlUi/Core/Types.h"

namespace juceRmlUi
{
	namespace gl2
	{
		class RmlShader;

		enum class ShaderType : uint8_t
		{
			DefaultTextured,
			DefaultColored,

			FullscreenColorMatrix,
			Fullscreen,

			Blur,

			Count
		};

		struct ShaderParams
		{
			uint32_t texture = 0;
			Rml::Matrix4f colorMatrix = Rml::Matrix4f::Identity();
			uint32_t blurPasses = 0;
			Rml::Vector2f blurScale{0,0};
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
			uint32_t stencilBuffer = 0;
		};

		struct CompiledShader
		{
			ShaderType type;
			ShaderParams params;
			RmlShader* shader = nullptr; // optional, only if 'type' is not sufficient
		};
	}
}
