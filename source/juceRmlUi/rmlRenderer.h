#pragma once

#include "RmlUi/Core/RenderInterface.h"

namespace juce
{
	class Image;
}

namespace juceRmlUi
{
	class DataProvider;

	class Renderer : public Rml::RenderInterface
	{
	public:
		explicit Renderer(DataProvider& _dataProvider);

		bool loadImage(juce::Image& _image, Rml::Vector2i& _textureDimensions, const Rml::String& _source) const;

		DataProvider& getDataProvider() const { return m_dataProvider; }

		void verticalFlip(Rml::Rectanglei& _rect) const;

		virtual Rml::TextureHandle loadTexture(juce::Image& _image) = 0;

		virtual void beginFrame(uint32_t _width, uint32_t _height);
		virtual void endFrame() {}

		virtual void onResize() {}

		uint32_t frameBufferWidth() const { return m_frameBufferWidth; }
		uint32_t frameBufferHeight() const { return m_frameBufferHeight; }

	private:
		uint32_t m_frameBufferWidth = 0;
		uint32_t m_frameBufferHeight = 0;

		DataProvider& m_dataProvider;
	};
}
