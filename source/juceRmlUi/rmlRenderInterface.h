#pragma once

#include "RmlUi/Core/RenderInterface.h"

namespace juceRmlUi
{
	class DataProvider;

	class RenderInterface : public Rml::RenderInterface
	{
	public:
		RenderInterface(DataProvider& _dataProvider);

		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle _geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle _texture) override;
		void EnableScissorRegion(bool _enable) override;
		void SetScissorRegion(Rml::Rectanglei _region) override;
		Rml::LayerHandle PushLayer() override;
		void CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, Rml::Span<const unsigned long long> _filters) override;
		void PopLayer() override;
		void EnableClipMask(bool _enable) override;
		void RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation) override;
		void SetTransform(const Rml::Matrix4f* _transform) override;
		Rml::TextureHandle SaveLayerAsTexture() override;
		Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
		Rml::CompiledFilterHandle CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters) override;
		void ReleaseFilter(Rml::CompiledFilterHandle _filter) override;
		Rml::CompiledShaderHandle CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters) override;
		void ReleaseShader(Rml::CompiledShaderHandle _shader) override;

		void beginFrame(uint32_t _width, uint32_t _height);
		void endFrame();

	private:
		DataProvider& m_dataProvider;

		struct CompiledGeometry
		{
			uint32_t vertexBuffer = 0;
			uint32_t indexBuffer = 0;
			size_t indexCount = 0;
			Rml::TextureHandle texture = 0;
		};

		struct LayerHandleData
		{
			uint32_t framebuffer = 0;
			uint32_t texture = 0;
		};

		uint32_t m_frameBufferWidth = 0;
		uint32_t m_frameBufferHeight = 0;

		std::stack<LayerHandleData*> m_layers;
	};
}
