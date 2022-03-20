#include "editor.h"

#include "uiObject.h"

#include "../synthLib/os.h"

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
			throw std::runtime_error("Failed to load json");

		m_jsonFilename = _jsonFilename;

		m_rootObject.reset(new UiObject(json));

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

		m_rootObject->createJuceTree(*this);

		m_scale = m_rootObject->getPropertyFloat("scale", 1.0f);
	}

	std::string Editor::exportToFolder(const std::string& _folder) const
	{
		if(!m_rootObject)
			return "Nothing to export";

		synthLib::createDirectory(_folder);

		std::string subfolder = m_jsonFilename;
		const auto dotIndex = m_jsonFilename.rfind('.');
		if(dotIndex != std::string::npos)
			subfolder = subfolder.substr(0, dotIndex);
		const auto underscoreIndex = m_jsonFilename.find('_');
		if(underscoreIndex != std::string::npos)
			subfolder = subfolder.substr(underscoreIndex+1);

		const auto folder = _folder + subfolder + '/';

		synthLib::createDirectory(folder);

		std::stringstream errors;

		auto writeFile = [this, &folder, &errors](const std::string& _name)
		{
			uint32_t dataSize;
			const auto data = m_interface.getResourceByFilename(_name, dataSize);

			FILE* hFile = fopen((folder + _name).c_str(), "wb");

			if(!hFile)
			{
				errors << "Failed to create file " << folder << _name << std::endl;
			}
			else
			{
				fwrite(data, dataSize, 1, hFile);
				fclose(hFile);
			}
		};

		auto writeData = [this, writeFile](const std::set<std::string>& _names, const std::string& _ext)
		{
			for (const auto& name : _names)
			{
				const auto dataName = name + _ext;
				writeFile(dataName);
			}
		};

		std::set<std::string> textures;
		std::set<std::string> fonts;

		m_rootObject->collectVariants(textures, "texture");
		m_rootObject->collectVariants(fonts, "fontFile");

		writeFile(m_jsonFilename);

		writeData(textures, ".png");
		writeData(fonts, ".ttf");

		return errors.str();
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

	const juce::Font& Editor::getFont(const std::string& _fontFile)
	{
		const auto it = m_fonts.find(_fontFile);
		if(it == m_fonts.end())
			throw std::runtime_error("Unable to find font named " + _fontFile);
		return it->second;
	}

	void Editor::registerComponent(const std::string& _name, juce::Component* _component)
	{
		const auto itExisting = m_componentsByName.find(_name);

		if(itExisting != m_componentsByName.end())
		{
			itExisting->second.push_back(_component);
		}
		else
		{
			m_componentsByName.insert(std::make_pair(_name, std::vector{_component}));
		}
	}

	const std::vector<juce::Component*>& Editor::findComponents(const std::string& _name, uint32_t _expectedCount/* = 0*/) const
	{
		const auto it = m_componentsByName.find(_name);
		if(it != m_componentsByName.end())
		{
			if(_expectedCount && it->second.size() != _expectedCount)
			{
				std::stringstream ss;
				ss << "Expected to find " << _expectedCount << " components named " << _name << " but found " << it->second.size();
				throw std::runtime_error(ss.str());
			}
			return it->second;
		}

		if(_expectedCount)
		{
			std::stringstream ss;
			ss << "Unable to find component named " << _name << ", expected to find " << _expectedCount << " components";
			throw std::runtime_error(ss.str());
		}

		static std::vector<juce::Component*> empty;
		return empty;
	}

	juce::Component* Editor::findComponent(const std::string& _name, bool _mustExist/* = true*/) const
	{
		const auto comps = findComponents(_name);
		if(comps.size() > 1)
			throw std::runtime_error("Failed to find unique component named " + _name + ", found more than one object with that name");
		if(_mustExist && comps.empty())
			throw std::runtime_error("Failed to find component named " + _name);
		return comps.empty() ? nullptr : comps.front();
	}
}
