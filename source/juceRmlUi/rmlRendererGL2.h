#pragma once

#include "rmlRenderInterfaceFilters.h"
#include "rmlRenderInterfaceShaders.h"

#include "RmlUi/Core/RenderInterface.h"

namespace juce
{
	class Image;
}

namespace juceRmlUi::gl2
{
	struct LayerHandleData;
	struct CompiledGeometry;
	struct ShaderParams;

	class RendererGL2 : public Rml::RenderInterface
	{
	public:
		RendererGL2(Rml::CoreInstance& _coreInstance);

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

		void beginFrame(uint32_t _width, uint32_t _height);
		void endFrame();

		static void checkGLError (const char* _file, int _line);
		
		uint32_t frameBufferWidth() const { return m_frameBufferWidth; }
		uint32_t frameBufferHeight() const { return m_frameBufferHeight; }

	private:
		void verticalFlip(Rml::Rectanglei& _rect) const;

		static void renderGeometry(const CompiledGeometry& _geom, RmlShader& _shader, const ShaderParams& _shaderParams);
		void renderBlur(const CompiledGeometry& _geom, const CompiledShader* _filter, const LayerHandleData* _source, const LayerHandleData* _target, Rml::BlendMode _blendMode);

		void onResize();

		LayerHandleData* createFrameBuffer() const;
		bool createFrameBuffer(LayerHandleData& _layer) const;
		static void deleteFrameBuffer(const LayerHandleData*& _layer);
		static void deleteFrameBuffer(const LayerHandleData& _layer);

		static void generateMipmaps();

		void copyFramebuffer(uint32_t _dest, uint32_t _source) const;

		static void setupBlending(Rml::BlendMode _blendMode);

		void writeFramebufferToFile(const std::string& _name, uint32_t _x = 0, uint32_t _y = 0, uint32_t _width = 0, uint32_t _height = 0) const;

		Rml::CoreInstance& m_coreInstance;

		Rml::CompiledGeometryHandle m_fullScreenGeometry = 0;

		std::vector<LayerHandleData*> m_layers;
		std::vector<LayerHandleData> m_tempFrameBuffers;
		std::vector<LayerHandleData> m_blurFrameBuffers;

		RenderInterfaceShaders m_shaders;
		RenderInterfaceFilters m_filters;
		Rml::Rectanglei m_scissorRegion;
		bool m_scissorEnabled = false;
		bool m_stencilEnabled = false;

		uint32_t m_frameBufferWidth = 0;
		uint32_t m_frameBufferHeight = 0;
	};

#define CHECK_OPENGL_ERROR do { RendererGL2::checkGLError (__FILE__, __LINE__); } while(0)
}
