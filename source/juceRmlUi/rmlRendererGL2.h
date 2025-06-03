#pragma once

#include "rmlRenderer.h"

#include "rmlRenderInterfaceFilters.h"
#include "rmlRenderInterfaceShaders.h"

namespace juce
{
	class Image;
}

namespace juceRmlUi::gl2
{
	struct LayerHandleData;
	struct CompiledGeometry;
	struct ShaderParams;

	class RendererGL2 : public Renderer
	{
	public:
		RendererGL2(DataProvider& _dataProvider);

		// Rml::RenderInterface overrides
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

		// Renderer overrides
		Rml::TextureHandle loadTexture(juce::Image& _image) override;

		void beginFrame(uint32_t _width, uint32_t _height);
		void endFrame();

		static void checkGLError (const char* _file, int _line);

	private:
		static void renderGeometry(const CompiledGeometry& _geom, RmlShader& _shader, const ShaderParams& _shaderParams);
		void renderBlur(const CompiledGeometry& _geom, const CompiledShader* _filter, const LayerHandleData* _source, const LayerHandleData* _target);

		void onResize();

		LayerHandleData* createFrameBuffer() const;
		bool createFrameBuffer(LayerHandleData& _layer) const;
		static void deleteFrameBuffer(const LayerHandleData*& _layer);
		static void deleteFrameBuffer(const LayerHandleData& _layer);

		void copyFramebuffer(uint32_t _dest, uint32_t _source) const;

		uint32_t m_frameBufferWidth = 0;
		uint32_t m_frameBufferHeight = 0;

		Rml::CompiledGeometryHandle m_fullScreenGeometry = 0;

		std::vector<LayerHandleData*> m_layers;
		std::vector<LayerHandleData> m_tempFrameBuffers;
		std::vector<LayerHandleData> m_blurFrameBuffers;

		RenderInterfaceShaders m_shaders;
		RenderInterfaceFilters m_filters;
	};

	#define CHECK_OPENGL_ERROR do { RendererGL2::checkGLError (__FILE__, __LINE__); } while(0)
}
