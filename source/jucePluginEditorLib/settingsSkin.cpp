#include "settingsSkin.h"

#include <set>

#include "pluginEditor.h"
#include "pluginEditorState.h"
#include "pluginProcessor.h"
#include "skin.h"

#include "baseLib/filesystem.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "juceUiLib/messageBox.h"

#include "RmlUi/Core/Element.h"

#include "synthLib/os.h"

namespace jucePluginEditorLib
{
	SettingsSkin::SettingsSkin(Processor& _processor) : SettingsPlugin(_processor)
	{
	}
	void SettingsSkin::createUi(Rml::Element* _root)
	{
		createToggleButton(_root, "btReloadViaF5", "reloadSkinViaF5", [this](bool){});

		createToggleButton(_root, "btEnableRmlUiDebugger", "enableRmlUiDebugger", [this](bool _enable)
		{
			m_processor.getEditorState()->getEditor()->getRmlComponent()->enableDebugger(_enable);
		});

		createToggleButton(_root, "btEnableMcpServer", "enableMcpServer", [this](bool _enable)
		{
			m_processor.setMcpServerEnabled(_enable);
		});

		auto* editorState = m_processor.getEditorState();

		if (auto* btOpenFolder = juceRmlUi::helper::findChild(_root, "btOpenFolder", false))
		{
			juceRmlUi::EventListener::AddClick(btOpenFolder, [this, editorState]
			{
				const auto dir = editorState->getSkinFolder();
				baseLib::filesystem::createDirectory(dir);
				juce::File(dir).revealToUser();
			});
		}

		bool loadedSkinIsPartOfList = false;

		std::set<std::pair<std::string, std::string>> knownSkins;	// folder, filename

		std::vector<Skin> skins;

		auto addSkinEntry = [this, editorState, &skins, &loadedSkinIsPartOfList, &knownSkins](const Skin& _skin)
		{
			// remove dupes by folder
			if(!_skin.folder.empty() && !knownSkins.insert({_skin.folder, _skin.filename}).second)
				return;

			const auto isCurrent = _skin == editorState->getCurrentSkin();

			if(isCurrent)
				loadedSkinIsPartOfList = true;

			skins.push_back(_skin);
		};

		for (const auto & skin : editorState->getIncludedSkins())
			addSkinEntry(skin);

		bool haveSkinsOnDisk = false;

		// find more skins on disk
		const auto modulePath = synthLib::getModulePath();

		// new: user documents folder
		std::vector<std::string> entries;
		baseLib::filesystem::getDirectoryEntries(entries, editorState->getSkinFolder());

		// old: next to plugin, kept for backwards compatibility
		std::vector<std::string> entriesModulePath;
		baseLib::filesystem::getDirectoryEntries(entriesModulePath, modulePath + "skins_" + m_processor.getProperties().name);
		entries.insert(entries.end(), entriesModulePath.begin(), entriesModulePath.end());

		for (const auto& entry : entries)
		{
			std::vector<std::string> files;
			baseLib::filesystem::getDirectoryEntries(files, entry);

			for (const auto& file : files)
			{
				const auto isJson = baseLib::filesystem::hasExtension(file, ".json");
				const auto isRml = baseLib::filesystem::hasExtension(file, ".rml");
				if(isJson || isRml)
				{
					if (isRml)
					{
						// ensure its not a template
						std::vector<uint8_t> rmlData;
						baseLib::filesystem::readFile(rmlData, file);
						constexpr char key[] = "<rml>";
						if (rmlData.size() < std::size(key))
							continue;
						if (std::memcmp(rmlData.data(), key, std::size(key) - 1) != 0)
							continue;
					}
					if(!haveSkinsOnDisk)
					{
						haveSkinsOnDisk = true;
//						skinMenu.addSeparator();
					}

					std::string skinPath = entry;
					if(entry.find(modulePath) == 0)
						skinPath = entry.substr(modulePath.size());
					skinPath = baseLib::filesystem::validatePath(skinPath);

					auto filename = baseLib::filesystem::getFilenameWithoutPath(file);

					auto displayName = PluginEditorState::createSkinDisplayName(file);
					const Skin skin{displayName, filename, skinPath, {}};

					addSkinEntry(skin);
				}
			}
		}

		if(!loadedSkinIsPartOfList)
			addSkinEntry(editorState->getCurrentSkin());

		// ____________
		// 

		auto* firstSkinEntry = juceRmlUi::helper::findChild(_root, "entry");

		auto newSkinEntry = [firstSkinEntry, this]()
		{
			return firstSkinEntry->GetParentNode()->AppendChild(firstSkinEntry->Clone());
		};

		bool isFirst = true;
		for (auto& skin : skins)
		{
			auto* entry = firstSkinEntry;

			if (isFirst)
				isFirst = false;
			else
				entry = newSkinEntry();

			auto* label = juceRmlUi::helper::findChild(entry, "lbName");
			label->SetInnerRML(Rml::StringUtilities::EncodeRml(skin.displayName));
			auto* type = juceRmlUi::helper::findChild(entry, "lbType");

			auto isEmbedded = skin.folder.empty();
			auto isCurrent = editorState->getCurrentSkin() == skin;

			// allow re-export if the skin is from a legacy folder
			const auto canExport = isEmbedded || skin.folder.find(editorState->getSkinFolder()) != 0;

			if (isEmbedded)
				type->SetInnerRML("Embedded");
			else
				type->SetInnerRML("Disk");

			auto* btActivate = juceRmlUi::helper::findChild(entry, "btActivate");
			auto* btExport = juceRmlUi::helper::findChild(entry, "btExport");

			juceRmlUi::helper::setVisible(btActivate, !isCurrent);
			juceRmlUi::helper::setVisible(btExport, canExport);

			entry->SetPseudoClass("current", isCurrent);

			juceRmlUi::EventListener::AddClick(btActivate, [this, editorState, skin]
			{
				juce::MessageManager::callAsync([this, editorState, skin]
				{
					editorState->loadSkin(skin);
				});
			});

			juceRmlUi::EventListener::AddClick(btExport, [this, skin]
			{
				exportSkin(skin);
			});
		}
	}

	void SettingsSkin::exportSkin(const Skin& _skin) const
	{
		auto* state = m_processor.getEditorState();

		const auto res = state->exportSkinToFolder(_skin, state->getSkinFolder());

		if(!res.empty())
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Export failed", "Failed to export skin:\n\n" + res, state->getUiRoot());
		}
		else
		{
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info, "Export finished", "Skin successfully exported", state->getUiRoot());
		}
	}
}
