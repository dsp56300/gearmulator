#include "imagePool.h"

#include "jucePluginData.h"

#include <map>

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	const ImagePool::ImageDataMap& ImagePool::getImageDataMap() const
	{
		if(m_imageDataMap.empty())
		{
			m_imageDataMap =
			{
				{Type::Link, {jucePluginData::link_png, jucePluginData::link_pngSize}},
				{Type::Lock, {jucePluginData::lock_png, jucePluginData::lock_pngSize}}
			};
		}
		return m_imageDataMap;
	}

	const juce::Image* ImagePool::getImage(const Type _type, const float _scale/* = 1.0f*/)
	{
		if(_scale >= 1.0f)
			return getImage(_type);

		const auto itScale = m_scaledImageMap.find(_type);
		if(itScale != m_scaledImageMap.end())
		{
			const auto itImage = itScale->second.find(_scale);
			if(itImage != itScale->second.end())
				return itImage->second.get();
		}

		auto* image = getImage(_type);
		if(!image)
			return nullptr;

		auto img = image->createCopy();

		const float initialWidth = static_cast<float>(img.getWidth());
		const float initialHeight = static_cast<float>(img.getHeight());

		const auto targetWidth = juce::roundToInt(initialWidth * _scale);
		const auto targetHeight = juce::roundToInt(initialHeight * _scale);

		while(img.getWidth() > targetWidth)
		{
			if((img.getWidth()>>1) < targetWidth)
			{
				img = img.rescaled(targetWidth, targetHeight, juce::Graphics::highResamplingQuality);
				break;
			}

			img = img.rescaled(img.getWidth()>>1, img.getHeight()>>1);
		}
		
		auto imageUniquePtr = std::make_unique<juce::Image>(img);

		const auto* rawPtr = imageUniquePtr.get();

		m_scaledImageMap[_type].insert(std::make_pair(_scale, std::move(imageUniquePtr)));

		return rawPtr;
	}

	const juce::Image* ImagePool::getImage(Type _type)
	{
		const auto itImage = m_imageMap.find(_type);

		if(itImage != m_imageMap.end())
			return itImage->second.get();

		const auto& datas = getImageDataMap();

		const auto it = datas.find(_type);
		if(it == datas.end())
			return nullptr;

		auto image = juce::ImageFileFormat::loadFrom(it->second.first, it->second.second);
		if(!image.isValid())
			return nullptr;

		auto imageUniquePtr = std::make_unique<juce::Image>(image);

		const auto* rawPtr = imageUniquePtr.get();

		m_imageMap.insert(std::make_pair(_type, std::move(imageUniquePtr)));

		return rawPtr;
	}
}
