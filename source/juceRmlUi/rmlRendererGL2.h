#pragma once

#include "rmlRenderInterfaceFilters.h"
#include "rmlRenderInterfaceShaders.h"

#include "RmlUi/Core/RenderInterface.h"

namespace juce
{
	class Image;
}

namespace juceRmlUi
{
	class DataProvider;

	class RendererGL2 : public Rml::RenderInterface
	{
	public:
		RendererGL2(DataProvider& _dataProvider);

		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle _geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle _texture) override;
		void EnableScissorRegion(bool _enable) override;
		void SetScissorRegion(Rml::Rectanglei _region) override;
		Rml::LayerHandle PushLayer() override;
		void CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, Rml::Span<const Rml::CompiledFilterHandle> _filters) override;
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

		DataProvider& getDataProvider() const { return m_dataProvider; }
		bool loadImage(juce::Image& _image, Rml::Vector2i& _textureDimensions, const Rml::String& _source) const;

		static Rml::TextureHandle loadTexture(juce::Image& _image);

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

		struct CompiledShader
		{
			uint32_t program = 0;
		};

		uint32_t m_frameBufferWidth = 0;
		uint32_t m_frameBufferHeight = 0;

		Rml::CompiledGeometryHandle m_fullScreenGeometry = 0;

		std::stack<LayerHandleData*> m_layers;

		RenderInterfaceShaders m_shaders;
		RenderInterfaceFilters m_filters;
	};
}
