#include "editor.h"

#include "uiObject.h"

#include "baseLib/filesystem.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace genericUI
{
	Editor::Editor(EditorInterface& _interface) : m_interface(_interface)
	{
	}

	void Editor::create(const std::string& _jsonFilename)
	{
		uint32_t jsonSize;
		const auto jsonData = m_interface.getResourceByFilename(_jsonFilename, jsonSize);

		juce::var json;
		const auto error = juce::JSON::parse(juce::String(std::string(jsonData, jsonSize)), json);

		if (error.failed())
			throw std::runtime_error("Failed to load json, " + error.getErrorMessage().toStdString());

		m_jsonFilename = _jsonFilename;

		m_rootObject.reset(new UiObject(nullptr, json));

		std::set<std::string> textures;
		m_rootObject->collectVariants(textures, "texture");

		for (const auto& texture : textures)
		{
			const auto dataName = texture + ".png";

			uint32_t dataSize;
			const auto* data = m_interface.getResourceByFilename(dataName, dataSize);
			if (!data)
				throw std::runtime_error("Failed to find image named " + dataName);
			auto drawable = juce::Drawable::createFromImageData(data, dataSize);
#ifdef _DEBUG
			drawable->setName(dataName);
#endif
			m_drawables.insert(std::make_pair(texture, std::move(drawable)));
		}

		std::set<std::string> fonts;
		m_rootObject->collectVariants(fonts, "fontFile");

		for(const auto& fontFile : fonts)
		{
			const auto dataName = fontFile + ".ttf";

			uint32_t dataSize;
			const auto* data = m_interface.getResourceByFilename(dataName, dataSize);
			if (!data)
				throw std::runtime_error("Failed to find font named " + dataName);
			auto font = juce::Font(juce::Typeface::createSystemTypefaceFor(data, dataSize));
			m_fonts.insert(std::make_pair(fontFile, std::move(font)));
		}

		m_scale = m_rootObject->getPropertyFloat("scale", 1.0f);

		// UI used to be created here, but instead it is converted to RML in the derived class
	}

	juce::Drawable* Editor::getImageDrawable(const std::string& _texture)
	{
		const auto it = m_drawables.find(_texture);
		return it == m_drawables.end() ? nullptr : it->second.get();
	}

	const juce::Font& Editor::getFont(const std::string& _fontFile)
	{
		const auto it = m_fonts.find(_fontFile);
		if(it == m_fonts.end())
			throw std::runtime_error("Unable to find font named " + _fontFile);
		return it->second;
	}
}
