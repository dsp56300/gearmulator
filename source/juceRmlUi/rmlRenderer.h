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

		virtual Rml::TextureHandle loadTexture(juce::Image& _image) = 0;

		DataProvider& getDataProvider() const { return m_dataProvider; }

	private:
		DataProvider& m_dataProvider;
	};
}
