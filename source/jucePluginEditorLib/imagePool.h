#pragma once

#include <map>
#include <cstddef>
#include <memory>

namespace juce
{
	class Image;
}

namespace jucePluginEditorLib
{
	class ImagePool
	{
	public:
		enum class Type
		{
			Lock,
			Link,

			Count
		};

		using ImageData = std::pair<const char*, size_t>;	// pointer to data, data size
		using ImageDataMap = std::map<Type, ImageData>;

		const ImageDataMap& getImageDataMap() const;

		const juce::Image* getImage(Type _type, float _scale);
		const juce::Image* getImage(Type _type);

	private:
		mutable ImageDataMap m_imageDataMap;

		std::map<Type, std::unique_ptr<juce::Image>> m_imageMap;
		std::map<Type, std::map<float,std::unique_ptr<juce::Image>>> m_scaledImageMap;
	};
}
