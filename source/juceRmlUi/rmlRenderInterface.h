#pragma once

#include "RmlUi/Core/RenderInterface.h"

namespace juceRmlUi
{
	class RenderInterface : public Rml::RenderInterface
	{
	public:
		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle _geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle _texture) override;
		void EnableScissorRegion(bool _enable) override;
		void SetScissorRegion(Rml::Rectanglei _region) override;

	private:
		struct CompiledGeometry
		{
			uint32_t vertexBuffer = 0;
			uint32_t indexBuffer = 0;
			size_t indexCount = 0;
			Rml::TextureHandle texture = 0;
		};
	};
}
