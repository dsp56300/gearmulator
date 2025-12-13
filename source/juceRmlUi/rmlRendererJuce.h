#pragma once

#include "RmlUi/Core/RenderInterface.h"

#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#   define IS_X64 1
#else
#   define IS_X64 0
#endif

#if defined(__aarch64__) || defined(__ARM_ARCH_8) || defined(_M_ARM64)
#	define IS_ARM64 1
#else
#	define IS_ARM64 0
#endif

namespace juce
{
	class Image;
	class LowLevelGraphicsContext;
	class Colour;
	class Graphics;
}

namespace juceRmlUi
{
	namespace rendererJuce
	{
		struct Image;
	}

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
		void endFrame(const juce::Image& _renderTarget);

		Rml::CompiledGeometryHandle	CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle _geometry) override;
		void RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;

		Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const uint8_t> _source, Rml::Vector2i _sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle _texture) override;

		void EnableScissorRegion(bool _enable) override;
		void SetScissorRegion(Rml::Rectanglei _region) override;

		static constexpr bool isX64() { return IS_X64; }

	private:
		void pushClip();

		bool m_scissorEnabled = false;
		Rml::Rectanglei m_scissorRegion;

		juce::Graphics* m_graphics = nullptr;

		std::unique_ptr<rendererJuce::Image> m_renderTarget;
		std::unique_ptr<juce::Image> m_renderImage;

		std::unordered_map<uint64_t, std::vector<std::unique_ptr<rendererJuce::Image>>> m_imagePool;

		bool m_pushed = false;
	};
}
