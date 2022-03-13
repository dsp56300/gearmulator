#include "editor.h"

#include "uiObject.h"

#include "BinaryData.h"

namespace genericUI
{
	Editor::Editor(const std::string& _json, VirusParameterBinding& _binding, Virus::Controller& _controller) : m_parameterBinding(_binding), m_controller(_controller)
	{
		juce::var json;

		const auto error = juce::JSON::parse(juce::String(_json), json);

		if (error.failed())
			throw std::runtime_error("Failed to load json");

		m_rootObject.reset(new UiObject(json));

		std::set<std::string> textures;
		m_rootObject->collectVariants(textures, "texture");

		for (const auto& texture : textures)
		{
			const auto dataName = texture + ".png";

			int dataSize;
			const auto* data = getResourceByFilename(dataName, dataSize);
			if (!data)
				throw std::runtime_error("Failed to find data named " + dataName);
			auto drawable = juce::Drawable::createFromImageData(data, dataSize);
			m_drawables.insert(std::make_pair(texture, std::move(drawable)));
		}

		m_rootObject->createJuceTree(*this);
	}

	juce::Drawable* Editor::getImageDrawable(const std::string& _texture)
	{
		const auto it = m_drawables.find(_texture);
		return it == m_drawables.end() ? nullptr : it->second.get();
	}

	std::unique_ptr<juce::Drawable> Editor::createImageDrawable(const std::string& _texture)
	{
		const auto existing = getImageDrawable(_texture);
		return existing ? existing->createCopy() : nullptr;
	}

	const char* Editor::getResourceByFilename(const std::string& _filename, int& _outDataSize)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			return BinaryData::getNamedResource(BinaryData::namedResourceList[i], _outDataSize);
		}

		_outDataSize = 0;
		return nullptr;
	}
}
