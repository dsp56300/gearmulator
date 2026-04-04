#pragma once

#ifdef __APPLE__

#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/Types.h>

class RenderInterface_Metal : public Rml::RenderInterface
{
public:
	// _device is id<MTLDevice>, passed as void* for C++ compatibility
	RenderInterface_Metal(Rml::CoreInstance& in_core_instance, void* _device);
	~RenderInterface_Metal();

	// Returns true if the renderer was successfully constructed.
	explicit operator bool() const { return m_initialized; }

	// The viewport should be updated whenever the window size changes.
	void SetViewport(int _width, int _height, int _offsetX = 0, int _offsetY = 0);

	// Sets up Metal state for taking rendering commands from RmlUi.
	// _drawable is id<CAMetalDrawable>, passed as void* for C++ compatibility
	void BeginFrame(void* _drawable);
	// Draws the result to the drawable and commits.
	void EndFrame();

	// Optional, can be used to clear the active render target.
	void Clear();

	// -- Inherited from Rml::RenderInterface --

	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle _handle, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle _handle) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> _sourceData, Rml::Vector2i _sourceDimensions) override;
	void ReleaseTexture(Rml::TextureHandle _textureHandle) override;

	void EnableScissorRegion(bool _enable) override;
	void SetScissorRegion(Rml::Rectanglei _region) override;

	void EnableClipMask(bool _enable) override;
	void RenderToClipMask(Rml::ClipMaskOperation _maskOperation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation) override;

	void SetTransform(const Rml::Matrix4f* _transform) override;

	Rml::LayerHandle PushLayer() override;
	void CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode,
		Rml::Span<const Rml::CompiledFilterHandle> _filters) override;
	void PopLayer() override;

	Rml::TextureHandle SaveLayerAsTexture() override;

	Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;

	Rml::CompiledFilterHandle CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters) override;
	void ReleaseFilter(Rml::CompiledFilterHandle _filter) override;

	Rml::CompiledShaderHandle CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters) override;
	void RenderShader(Rml::CompiledShaderHandle _shaderHandle, Rml::CompiledGeometryHandle _geometryHandle, Rml::Vector2f _translation,
		Rml::TextureHandle _texture) override;
	void ReleaseShader(Rml::CompiledShaderHandle _shaderHandle) override;

	// Can be passed to RenderGeometry() to enable texture rendering without changing the bound texture.
	static constexpr Rml::TextureHandle TextureEnableWithoutBinding = Rml::TextureHandle(-1);
	// Can be passed to RenderGeometry() to leave the bound texture and used pipeline unchanged.
	static constexpr Rml::TextureHandle TexturePostprocess = Rml::TextureHandle(-2);

	// -- Utility functions for clients --

	const Rml::Matrix4f& GetTransform() const;
	void ResetPipeline();

private:
	void RenderFiltersInternal(Rml::Span<const Rml::CompiledFilterHandle> _filterHandles);
	void RenderBlurInternal(float _sigma, const void* _sourceDestination, const void* _temp);

	struct Impl;
	Rml::UniquePtr<Impl> m_impl;
	bool m_initialized = false;
};

/**
    Helper functions for the Metal renderer.
 */
namespace RmlMetal {

// Returns true if Metal is available on this system.
bool IsSupported();

} // namespace RmlMetal

#endif // __APPLE__
