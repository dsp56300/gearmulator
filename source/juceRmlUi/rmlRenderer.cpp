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

	void Renderer::verticalFlip(Rml::Rectanglei& _rect) const
	{
		_rect.p0.y = static_cast<int>(m_frameBufferHeight) - _rect.p0.y;
		_rect.p1.y = static_cast<int>(m_frameBufferHeight) - _rect.p1.y;

		if (_rect.p0.y > _rect.p1.y)
			std::swap(_rect.p0.y, _rect.p1.y);

	}

	void Renderer::beginFrame(const uint32_t _width, const uint32_t _height)
	{
		if (_width != m_frameBufferWidth || _height != m_frameBufferHeight)
		{
			m_frameBufferWidth = _width;
			m_frameBufferHeight = _height;
			onResize();
		}

		if (m_frameBufferWidth == 0 || m_frameBufferHeight == 0)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Invalid frame buffer size: %u x %u", m_frameBufferWidth, m_frameBufferHeight);
			assert(false && "invalid frame buffer size");
		}
	}
}
