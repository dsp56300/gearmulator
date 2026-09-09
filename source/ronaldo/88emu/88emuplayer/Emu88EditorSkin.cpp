#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "Emu88EditorWindows.h"
#include "BinaryData.h"
#include "jucePluginData.h"
#include "juceUiLib/messageBox.h"

#include <algorithm>
#include <set>
#include <string_view>

namespace emu88Player
{
	using namespace editor;

	const char* Editor::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		if(!m_skin.folder.empty())
		{
			const auto found = m_fileCache.find(_name);
			if(found != m_fileCache.end())
			{
				_dataSize = static_cast<uint32_t>(found->second.size());
				return found->second.data();
			}

			const auto file = juce::File(m_skin.folder).getChildFile(_name);
			if(file.existsAsFile())
			{
				juce::MemoryBlock data;
				if(file.loadFileAsData(data))
				{
					auto& cached = m_fileCache[_name];
					const auto* begin = static_cast<const char*>(data.getData());
					cached.assign(begin, begin + data.getSize());
					_dataSize = static_cast<uint32_t>(cached.size());
					return cached.data();
				}
			}
		}

		if(const auto* result = findBinaryResource(_name, _dataSize, BinaryData::namedResourceListSize,
			BinaryData::originalFilenames, BinaryData::namedResourceList, BinaryData::getNamedResource))
			return result;
		if(const auto* result = findBinaryResource(_name, _dataSize, jucePluginData::namedResourceListSize,
			jucePluginData::originalFilenames, jucePluginData::namedResourceList, jucePluginData::getNamedResource))
			return result;

		_dataSize = 0;
		throw std::runtime_error("Missing 88emuPlayer UI resource: " + _name);
	}

	std::vector<std::string> Editor::getAllFilenames()
	{
		std::vector<std::string> result;
		if(!m_skin.folder.empty())
		{
			juce::Array<juce::File> files;
			juce::File(m_skin.folder).findChildFiles(files, juce::File::findFiles, false);
			for(const auto& file : files)
				result.push_back(file.getFileName().toStdString());
		}
		for(int i = 0; i < BinaryData::namedResourceListSize; ++i)
			result.emplace_back(binaryResourceFileName(BinaryData::originalFilenames[i]));
		for(int i = 0; i < jucePluginData::namedResourceListSize; ++i)
			result.emplace_back(binaryResourceFileName(jucePluginData::originalFilenames[i]));
		return result;
	}

	Editor::Skin Editor::readSkinFromConfig() const
	{
		Skin skin;
		skin.displayName = m_processor.config().getValue("skinDisplayName", "88emuPlayer").toStdString();
		skin.filename = m_processor.config().getValue("skinFile", "emu88Player.rml").toStdString();
		skin.folder = m_processor.config().getValue("skinFolder", "").toStdString();
		// An empty folder selects the sole bundled skin, including older saved filenames.
		if(skin.folder.empty() || skin.filename.empty())
			skin = {"88emuPlayer", "emu88Player.rml", {}};
		return skin;
	}

	void Editor::writeSkinToConfig(const Skin& _skin) const
	{
		auto& config = m_processor.config();
		config.setValue("skinDisplayName", juce::String::fromUTF8(_skin.displayName.c_str()));
		config.setValue("skinFile", juce::String::fromUTF8(_skin.filename.c_str()));
		config.setValue("skinFolder", juce::String::fromUTF8(_skin.folder.c_str()));
		config.saveIfNeeded();
	}

	std::string Editor::skinFolder() const
	{
		return juce::File(m_processor.dataFolder()).getChildFile("skins").getFullPathName().toStdString();
	}

	std::vector<Editor::Skin> Editor::findSkins() const
	{
		std::vector<Skin> result{{"88emuPlayer", "emu88Player.rml", {}}};
		const auto root = juce::File(skinFolder());
		juce::Array<juce::File> folders;
		root.findChildFiles(folders, juce::File::findDirectories, false);
		for(const auto& folder : folders)
		{
			juce::Array<juce::File> files;
			folder.findChildFiles(files, juce::File::findFiles, false, "*.rml");
			for(const auto& file : files)
			{
				const auto contents = file.loadFileAsString().trimStart();
				if(!contents.startsWith("<rml>"))
					continue;
				result.push_back({folder.getFileName().toStdString(), file.getFileName().toStdString(),
				                  folder.getFullPathName().toStdString()});
				break;
			}
		}
		return result;
	}

	void Editor::loadSkin(const Skin& _skin)
	{
		destroyRmlUi();
		m_displayRevision = 0;
		m_skin = _skin;
		try
		{
			createRmlUi();
			writeSkinToConfig(m_skin);
		}
		catch(const std::runtime_error& error)
		{
			const auto failedSkin = m_skin;
			destroyRmlUi();
			m_skin = {"88emuPlayer", "emu88Player.rml", {}};
			createRmlUi();
			writeSkinToConfig(m_skin);
			if(!failedSkin.folder.empty())
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning,
					"88emuPlayer - Skin load failed", error.what(), this);
		}
		if(m_settingsWindow)
		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(m_settingsWindow->rmlComponent());
			populateSkinSettings();
		}
	}

	void Editor::exportEmbeddedSkin()
	{
		const auto destination = juce::File(skinFolder()).getChildFile("88emuPlayer");
		if(!destination.createDirectory())
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Export failed",
				"Unable to create the 88emuPlayer skin folder.", this);
			return;
		}

		for(int i = 0; i < BinaryData::namedResourceListSize; ++i)
		{
			int size = 0;
			const auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			const auto filename = juce::File(BinaryData::originalFilenames[i]).getFileName();
			if(!data || size <= 0 || !destination.getChildFile(filename).replaceWithData(data, static_cast<size_t>(size)))
			{
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Export failed",
					"Unable to export " + filename.toStdString() + '.', this);
				return;
			}
		}
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info, "Export finished",
			"The embedded hardware skin was exported successfully.", this);
	}
}
