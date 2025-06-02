#include "rmlRenderer.h"

#include <cassert>

#include "rmlDataProvider.h"

#include "baseLib/filesystem.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Log.h"

namespace juceRmlUi
{
	Renderer::Renderer(DataProvider& _dataProvider) : m_dataProvider(_dataProvider)
	{
	}

	bool Renderer::loadImage(juce::Image& _image, Rml::Vector2i& _textureDimensions, const Rml::String& _source) const
	{
		uint32_t fileSize;
		auto* ptr = m_dataProvider.getResourceByFilename(baseLib::filesystem::getFilenameWithoutPath(_source), fileSize);

		if (!ptr)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "image file not found: %s", _source.c_str());
			assert(false && "file not found");
			return false;
		}

		// Load the texture from the file
		_image = juce::ImageFileFormat::loadFrom(ptr, fileSize);
		if (_image.isNull())
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load image from source %s", _source.c_str());
			assert(false && "failed to load image");
			return false;
		}

		_textureDimensions.x = _image.getWidth();
		_textureDimensions.y = _image.getHeight();

		return true;
	}
}
