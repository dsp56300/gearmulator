#pragma once

#include "RmlUi/Core/RenderInterface.h"

namespace juce
{
	class LowLevelGraphicsContext;
	class Colour;
	class Image;
	class Graphics;
}

namespace juceRmlUi
{
	class RendererJuce : public Rml::RenderInterface
	{
	public:
		RendererJuce() = delete;
		RendererJuce(Rml::CoreInstance& _coreInstance);

		~RendererJuce() override;

		RendererJuce(const RendererJuce&) = delete;
		RendererJuce(RendererJuce&&) = delete;

		RendererJuce& operator=(const RendererJuce&) = delete;
		RendererJuce& operator=(RendererJuce&&) = delete;

		void beginFrame(juce::Graphics& _g);
		void endFrame();

		Rml::CompiledGeometryHandle	CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle _geometry) override;
		void RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;

		Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const uint8_t> _source, Rml::Vector2i _sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle _texture) override;

		void EnableScissorRegion(bool _enable) override;
		void SetScissorRegion(Rml::Rectanglei _region) override;

	private:
		juce::Image& blit(const juce::Image& _img, int _srcX, int _srcY, int _srcW, int _srcH, const juce::Colour& _color);

		void pushClip();

		bool m_scissorEnabled = false;
		Rml::Rectanglei m_scissorRegion;

		juce::Graphics* m_graphics = nullptr;

		std::unique_ptr<juce::Image> m_tempImage = nullptr;

		bool m_pushed = false;
	};
}
