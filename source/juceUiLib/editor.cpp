#include "editor.h"

#include "uiObject.h"

#include "baseLib/filesystem.h"

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

		m_rootObject->createJuceTree(*this);
		m_rootObject->createTabGroups(*this);
		m_rootObject->createControllerLinks(*this);
		m_rootObject->registerTemplates(*this);

		m_scale = m_rootObject->getPropertyFloat("scale", 1.0f);
	}

	std::string Editor::exportToFolder(const std::string& _folder) const
	{
		if(!m_rootObject)
			return "Nothing to export";

		baseLib::filesystem::createDirectory(_folder);

		std::string subfolder = m_jsonFilename;
		const auto dotIndex = m_jsonFilename.rfind('.');
		if(dotIndex != std::string::npos)
			subfolder = subfolder.substr(0, dotIndex);
		const auto underscoreIndex = m_jsonFilename.find('_');
		if(underscoreIndex != std::string::npos)
			subfolder = subfolder.substr(underscoreIndex+1);

		const auto folder = _folder + subfolder + '/';

		baseLib::filesystem::createDirectory(folder);

		std::stringstream errors;

		auto writeFile = [this, &folder, &errors](const std::string& _name)
		{
			uint32_t dataSize;
			const auto data = m_interface.getResourceByFilename(_name, dataSize);

			FILE* hFile = fopen((folder + _name).c_str(), "wb");

			if(!hFile)
			{
				errors << "Failed to create file " << folder << _name << '\n';
			}
			else
			{
				const auto writtenCount = fwrite(data, 1, dataSize, hFile);
				(void)fclose(hFile);
				if(writtenCount != dataSize)
					errors << "Failed to write " << dataSize << " bytes to " << folder << _name << '\n';
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

		const auto param = _component->getProperties()["parametername"].toString().toStdString();
		if(param.empty())
			return;

		const auto itParamExisting = m_componentsByParameter.find(param);

		if(itParamExisting != m_componentsByParameter.end())
		{
			itParamExisting->second.push_back(_component);
		}
		else
		{
			m_componentsByParameter.insert(std::make_pair(param, std::vector{_component}));
		}
	}

	void Editor::registerTabGroup(TabGroup* _group)
	{
		const auto& n = _group->getName();

		if(m_tabGroupsByName.find(n) != m_tabGroupsByName.end())
			throw std::runtime_error("tab group " + n + " is already defined");

		m_tabGroupsByName.insert(std::make_pair(n, _group));
	}

	const std::vector<juce::Component*>& Editor::findComponents(const std::map<std::string, std::vector<juce::Component*>>& _components, const std::string& _name, const uint32_t _expectedCount, const std::string& _typename)
	{
		const auto it = _components.find(_name);
		if(it != _components.end())
		{
			if(_expectedCount && it->second.size() != _expectedCount)
			{
				std::stringstream ss;
				ss << "Expected to find " << _expectedCount << " components with " << _typename << ' ' << _name << " but found " << it->second.size();
				throw std::runtime_error(ss.str());
			}
			return it->second;
		}

		if(_expectedCount)
		{
			std::stringstream ss;
			ss << "Unable to find component with " << _typename << ' ' << _name << ", expected to find " << _expectedCount << " components";
			throw std::runtime_error(ss.str());
		}

		static std::vector<juce::Component*> empty;
		return empty;
	}

	const std::vector<juce::Component*>& Editor::findComponents(const std::string& _name, const uint32_t _expectedCount/* = 0*/) const
	{
		return findComponents(m_componentsByName, _name, _expectedCount, "name");
	}

	const std::vector<juce::Component*>& Editor::findComponentsByParam(const std::string& _name, const uint32_t _expectedCount) const
	{
		return findComponents(m_componentsByParameter, _name, _expectedCount, "parameter");
	}

	juce::Component* Editor::findComponent(const std::string& _name, const bool _mustExist/* = true*/) const
	{
		const auto comps = findComponents(_name);
		if(comps.size() > 1)
			throw std::runtime_error("Failed to find unique component named " + _name + ", found more than one object with that name");
		if(_mustExist && comps.empty())
			throw std::runtime_error("Failed to find component named " + _name);
		return comps.empty() ? nullptr : comps.front();
	}

	juce::Component* Editor::findComponentByParam(const std::string& _param, const bool _mustExist) const
	{
		const auto comps = findComponentsByParam(_param);
		if(comps.size() > 1)
			throw std::runtime_error("Failed to find unique component with parameter " + _param + ", found more than one object with that parameter");
		if(_mustExist && comps.empty())
			throw std::runtime_error("Failed to find component with parameter " + _param);
		return comps.empty() ? nullptr : comps.front();
	}

	size_t Editor::getConditionCountRecursive() const
	{
		return m_rootObject ? m_rootObject->getConditionCountRecursive() : 0;
	}

	size_t Editor::getControllerLinkCountRecursive() const
	{
		return m_rootObject ? m_rootObject->getControllerLinkCountRecursive() : 0;
	}

	void Editor::registerTemplate(const std::shared_ptr<UiObject>& _value)
	{
		const auto name = _value->getName();

		if (name.empty())
			throw std::runtime_error("Every template needs to have a name");

		if (m_templates.find(name) != m_templates.end())
			throw std::runtime_error("Template with name '" + name + "' exists more than once");

		m_templates.insert({ name, _value });
	}

	void Editor::setEnabled(juce::Component& _component, const bool _enable)
	{
		if(_component.getProperties().contains("disabledAlpha"))
		{
			const float a = _component.getProperties()["disabledAlpha"];

			_component.setAlpha(_enable ? 1.0f : a);
			_component.setEnabled(_enable);
		}
		else
		{
			_component.setVisible(_enable);
		}
	}

	void Editor::setCurrentPart(const uint8_t _part)
	{
		m_rootObject->setCurrentPart(*this, _part);
	}

	void Editor::updateKeyValueConditions(const std::string& _key, const std::string& _value) const
	{
		m_rootObject->updateKeyValueConditions(_key, _value);
	}

	std::shared_ptr<UiObject> Editor::getTemplate(const std::string& _name) const
	{
		const auto& it = m_templates.find(_name);
		if (it == m_templates.end())
			return {};
		return it->second;
	}

	bool Editor::resizeDrawableImage(juce::DrawableImage& _drawable, const uint32_t _percent)
	{
		if(_percent == 100)
			return true;
		if(_percent < 1)
			return false;

		auto image = _drawable.getImage();
		const auto x = image.getBounds().getX();
		const auto y = image.getBounds().getY();
		const auto w = image.getWidth();
		const auto h = image.getHeight();

		const int percent = static_cast<int>(_percent);

		image = image.rescaled(w * percent / 100, h * percent / 100);

		_drawable.setImage(image);
		auto b = image.getBounds();
		b.setSize(w, h);
		b.setPosition(x, y);
		_drawable.setBounds(b);
		_drawable.setBoundingBox(b.toFloat());
		return true;
	}

	void Editor::paint(juce::Graphics& g)
	{
#if JUCE_MAJOR_VERSION >= 8 && defined(_WIN32)
		// This makes a huge difference on Windows, especially when the whole window is downscaled
		g.getInternalContext().setInterpolationQuality(juce::Graphics::highResamplingQuality);
#endif
		Component::paint(g);
	}

	bool Editor::selectTabWithComponent(const juce::Component* _component) const
	{
		for (const auto& it : m_tabGroupsByName)
		{
			auto* tabGroup = it.second;
			if (tabGroup->selectTabWithComponent(_component))
				return true;
		}
		return false;
	}
}
