#include "pluginEditorState.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "baseLib/filesystem.h"

#include "patchmanager/patchmanager.h"

#include "juceUiLib/editor.h"
#include "juceUiLib/messageBox.h"

#include "dsp56kBase/logging.h"

#include "juceRmlUi/juceRmlComponent.h"

#include "RmlUi/Core/ElementDocument.h"

namespace
{
	bridgeLib::PluginDesc getPluginDesc(const pluginLib::Processor& _p)
	{
		bridgeLib::PluginDesc pd;
		_p.getPluginDesc(pd);
		return pd;
	}
}

namespace jucePluginEditorLib
{
PluginEditorState::PluginEditorState(Processor& _processor, pluginLib::Controller& _controller, std::vector<Skin> _includedSkins)
	: m_processor(_processor), m_includedSkins(std::move(_includedSkins))
{
	if (m_processor.getConfig().getBoolValue("supportDspBridge", false))
		m_remoteServerList.reset(new bridgeClient::ServerList(getPluginDesc(_processor)));

	juce::File(getSkinFolder()).createDirectory();

	// point embedded skins to public data folder if they're not embedded
	for (auto& skin : m_includedSkins)
	{
		if(skin.folder.empty() && !m_processor.findResource(skin.filename))
			skin.folder = baseLib::filesystem::validatePath(getSkinFolder() + skin.displayName);
	}
}

PluginEditorState::~PluginEditorState()
= default;

int PluginEditorState::getWidth() const
{
	return m_editor ? m_editor->getDefaultWidth() : 0;
}

int PluginEditorState::getHeight() const
{
	return m_editor ? m_editor->getDefaultHeight() : 0;
}

bool PluginEditorState::resizeEditor(const int _width, const int _height) const
{
	if (!m_editor)
		return false;
	return m_editor->setSize(_width, _height);
}

const std::vector<Skin>& PluginEditorState::getIncludedSkins()
{
	return m_includedSkins;
}

std::string PluginEditorState::createSkinDisplayName(std::string _filename)
{
	const auto pathEndPos = _filename.find_last_of("/\\");
	if(pathEndPos != std::string::npos)
		_filename = _filename.substr(pathEndPos+1);

	auto displayName = baseLib::filesystem::stripExtension(_filename);

	const auto isJson = baseLib::filesystem::hasExtension(_filename, ".json");
	if (isJson)
		displayName += " (legacy)";

	return displayName;
}

juce::Component* PluginEditorState::getUiRoot() const
{
	auto* e = m_editor.get();
	if (!e)
		return {};
	if (auto* c = e->getRmlComponent())
		return c;
	return {};
}

void PluginEditorState::loadDefaultSkin()
{
	Skin skin = readSkinFromConfig();

	if(skin.filename.empty())
	{
		skin = m_includedSkins[0];
	}

	loadSkin(skin);
}

void PluginEditorState::setPerInstanceConfig(const std::vector<uint8_t>& _data)
{
	m_instanceConfig = _data;

	if(m_editor && !m_instanceConfig.empty())
		getEditor()->setPerInstanceConfig(m_instanceConfig);
}

void PluginEditorState::getPerInstanceConfig(std::vector<uint8_t>& _data)
{
	if(m_editor)
	{
		m_instanceConfig.clear();
		getEditor()->getPerInstanceConfig(m_instanceConfig);
	}

	if(!m_instanceConfig.empty())
		_data.insert(_data.end(), m_instanceConfig.begin(), m_instanceConfig.end());
}

std::string PluginEditorState::getSkinFolder() const
{
	return getSkinFolder(m_processor.getDataFolder());
}

std::string PluginEditorState::getSkinFolder(const std::string& _processorDataFolder)
{
	return baseLib::filesystem::validatePath(_processorDataFolder + "skins/");
}

std::string PluginEditorState::getSkinSubfolder(const Skin& _skin, const std::string& _folder)
{
	std::string subfolder = baseLib::filesystem::stripExtension(_skin.filename);

	const auto folder = _folder + subfolder + '/';
	return folder;
}

bool PluginEditorState::loadSkin(const Skin& _skin, const uint32_t _fallbackIndex/* = 0*/)
{
	auto skin = _skin;

	bool hasSettingsOpened = false;

	if (m_editor)
	{
		m_instanceConfig.clear();
		getEditor()->getPerInstanceConfig(m_instanceConfig);

		hasSettingsOpened = m_editor->settingsOpened();
		m_editor.reset();
	}

	m_rootScale = 1.0f;

	try
	{
		// if the embedded skin cannot be found, use skin folder as fallback
		if(skin.folder.empty() && !m_processor.findResource(skin.filename))
		{
			skin.folder = baseLib::filesystem::validatePath(getSkinFolder() + skin.displayName);
		}

		if (!skin.folder.empty())
		{
			// if a folder is specified, the skin must be on disk. We check this explicitly here because a file that has an identical name might get picked
			// from embedded resources otherwise
			const auto skinFile = Editor::getAbsoluteSkinFolder(m_processor, skin.folder) + skin.filename;
			juce::File f(juce::String::fromUTF8(skinFile.c_str()));
			if (!f.existsAsFile())
				throw std::runtime_error("Skin file '" + skinFile + "' not found on disk");
		}

		m_editor.reset(createEditor(skin));
		m_editor->create();

		m_editor->getRmlComponent()->enableDebugger(m_processor.getConfig().getBoolValue("enableRmlUiDebugger", false));

		getEditor()->onOpenMenu.addListener([this](Editor*, const Rml::Event& _event)
		{
			openMenu(_event);
		});

		m_rootScale = m_editor->getScale();

		m_currentSkin = m_editor->getSkin();

		if (m_currentSkin.filename != skin.filename && !skin.folder.empty())	// empty folder = integrated skin => for development only
		{
			genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Info, m_editor->getProcessor().getProperties().name, 
				"The skin\n"
				"    '" + baseLib::filesystem::stripExtension(m_currentSkin.filename) + "'\n"
				" is a legacy skin and has been automatically converted from json format to RmlUI.\n"
				"\n"
				"The automatic conversion can be inaccurate. Please contact the author of this skin and ask for an update.\n"
				"\n"
				"Do you want to switch to the default skin instead?", [this](const genericUI::MessageBox::Result _result)
				{
					if (_result == genericUI::MessageBox::Result::Yes)
					{
						auto defaultSkin = getIncludedSkins().front();
						loadSkin(defaultSkin);
					}
				}
			);
		}
		writeSkinToConfig(m_currentSkin);

		if(evSkinLoaded)
			evSkinLoaded(m_editor->getRmlComponent());

		if(!m_instanceConfig.empty())
			getEditor()->setPerInstanceConfig(m_instanceConfig);

		getEditor()->showSettings(hasSettingsOpened);

		auto* doc = m_editor->getDocument();

		juceRmlUi::EventListener::Add(doc, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			if (juceRmlUi::helper::getKeyIdentifier(_event) == Rml::Input::KI_F5)
			{
				if (!m_processor.getConfig().getBoolValue("reloadSkinViaF5", false))
					return;

				_event.StopPropagation();
				juce::MessageManager::callAsync([this]
				{
					loadSkin(m_currentSkin);
				});
			}
		});

		return true;
	}
	catch(const std::runtime_error& _err)
	{
		LOG("ERROR: Failed to create editor: " << _err.what());

		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, m_processor.getProperties().name + " - Skin load failed", _err.what());

		m_editor.reset();

		if(_fallbackIndex >= m_includedSkins.size())
			return false;

		return loadSkin(m_includedSkins[_fallbackIndex], _fallbackIndex + 1);
	}
}

void PluginEditorState::setGuiScale(const int _scale) const
{
	if(evSetGuiScale)
		evSetGuiScale(_scale);
}

std::string PluginEditorState::exportSkinToFolder(const Skin& _skin, const std::string& _folder) const
{
	if (!m_editor)
		return "no editor to export skin";

	if (_skin.files.empty())
		return "no files to export";

	baseLib::filesystem::createDirectory(_folder);

	const auto folder = getSkinSubfolder(_skin, _folder);

	baseLib::filesystem::createDirectory(folder);

	std::stringstream errors;

	auto writeFile = [this, &folder, &errors](const std::string& _name)
	{
		const auto res = m_processor.findResource(_name);

		if (!res)
		{
			errors << "Failed to find resource " << _name << '\n';
			return;
		}

		FILE* hFile = baseLib::filesystem::openFile(folder + _name, "wb");

		if(!hFile)
		{
			errors << "Failed to create file " << folder << _name << '\n';
		}
		else
		{
			const auto data = res->first;
			const auto dataSize = res->second;
			const auto writtenCount = fwrite(data, 1, dataSize, hFile);
			(void)fclose(hFile);
			if(writtenCount != dataSize)
				errors << "Failed to write " << dataSize << " bytes to " << folder << _name << '\n';
		}
	};

	for (const auto& file : _skin.files)
		writeFile(file);

	return errors.str();
}

Editor* PluginEditorState::getEditor() const
{
	return m_editor.get();
}

void PluginEditorState::enableDspBridge(const bool _enable)
{
	if (_enable && !m_remoteServerList)
	{
		m_remoteServerList.reset(new bridgeClient::ServerList(getPluginDesc(m_processor)));
	}
	else if (!_enable && m_remoteServerList)
	{
		m_remoteServerList.reset();
	}
}

void PluginEditorState::openMenu(const Rml::Event& _event)
{
	if(getEditor()->openContextMenuForParameter(_event))
		return;

	const auto& config = m_processor.getConfig();
    const auto scale = juce::roundToInt(config.getDoubleValue("scale", 100));

	juceRmlUi::Menu menu;

	juceRmlUi::Menu scaleMenu;
	scaleMenu.addEntry("50%", scale == 50, [this] { setGuiScale(50); });
	scaleMenu.addEntry("65%", scale == 65, [this] { setGuiScale(65); });
	scaleMenu.addEntry("75%", scale == 75, [this] { setGuiScale(75); });
	scaleMenu.addEntry("85%", scale == 85, [this] { setGuiScale(85); });
	scaleMenu.addEntry("100%", scale == 100, [this] { setGuiScale(100); });
	scaleMenu.addEntry("125%", scale == 125, [this] { setGuiScale(125); });
	scaleMenu.addEntry("150%", scale == 150, [this] { setGuiScale(150); });
	scaleMenu.addEntry("175%", scale == 175, [this] { setGuiScale(175); });
	scaleMenu.addEntry("200%", scale == 200, [this] { setGuiScale(200); });
	scaleMenu.addEntry("250%", scale == 250, [this] { setGuiScale(250); });
	scaleMenu.addEntry("300%", scale == 300, [this] { setGuiScale(300); });

	menu.addSubMenu("GUI Scale", std::move(scaleMenu));

	menu.addSeparator();

	auto& regions = m_processor.getController().getParameterDescriptions().getRegions();

	if(!regions.empty())
	{
		const auto part = m_processor.getController().getCurrentPart();

		juceRmlUi::Menu lockRegions;

		auto& locking = m_processor.getController().getParameterLocking();

		lockRegions.addEntry("Unlock All", [&, part]
		{
			for (const auto& region : regions)
				locking.unlockRegion(part, region.first);
		});

		lockRegions.addEntry("Lock All", [&, part]
		{
			for (const auto& region : regions)
				locking.lockRegion(part, region.first);
		});

		lockRegions.addSeparator();

		std::map<std::string, pluginLib::ParameterRegion> sortedRegions;
		for (const auto& region : regions)
			sortedRegions.insert(region);

		for (const auto& region : sortedRegions)
		{
			lockRegions.addEntry(region.second.getName(), m_processor.getController().getParameterLocking().isRegionLocked(part, region.first), [this, id=region.first, part]
			{
				auto& locking = m_processor.getController().getParameterLocking();

				if(locking.isRegionLocked(part, id))
					locking.unlockRegion(part, id);
				else
					locking.lockRegion(part, id);
			});
		}

		menu.addSubMenu("Lock Regions", std::move(lockRegions));
	}

	initContextMenu(menu);

	if(auto* editor = dynamic_cast<Editor*>(getEditor()))
	{
		if(auto* pm = editor->getPatchManager())
		{
			menu.addSeparator();

#ifdef JUCE_MAC
			const std::string ctrlName = "Cmd";
#else
			const std::string ctrlName = "Ctrl";
#endif
			{
				menu.addEntry("Copy current Patch to Clipboard (" + ctrlName + "+C)", [editor]
				{
					editor->copyCurrentPatchToClipboard();
				});
			}

			{
				auto patches = pm->getPatchesFromClipboard();
				if(!patches.empty())
				{
					menu.addEntry("Replace current Patch from Clipboard (" + ctrlName + "+V)", [editor]
					{
						editor->replaceCurrentPatchFromClipboard();
					});
				}
			}
		}
	}

	{
		const auto allowAdvanced = config.getBoolValue("allow_advanced_options", false);

		juceRmlUi::Menu advancedMenu;
		advancedMenu.addEntry("Enable Advanced Options", true, allowAdvanced, [this, allowAdvanced]
		{
			if(!allowAdvanced)
			{
				genericUI::MessageBox::showOkCancel(
					genericUI::MessageBox::Icon::Warning, 
					"Warning", 
					"Changing these settings may cause instability of the plugin.\n\nPlease confirm to continue.", 
					[this](const genericUI::MessageBox::Result _result)
				{
					if (_result == genericUI::MessageBox::Result::Ok)
						m_processor.getConfig().setValue("allow_advanced_options", true);
				});
			}
			else
			{
				m_processor.getConfig().setValue("allow_advanced_options", juce::var(false));
			}
		});

		advancedMenu.addSeparator();

		if(initAdvancedContextMenu(advancedMenu, allowAdvanced))
		{
			menu.addSeparator();
			menu.addSubMenu("Advanced...", std::move(advancedMenu));
		}
	}

	menu.runModal(_event, 16);
}

Skin PluginEditorState::readSkinFromConfig() const
{
	auto& config = m_processor.getConfig();

	Skin skin;
	skin.displayName = config.getValue("skinDisplayName", "").toStdString();
	skin.filename = config.getValue("skinFile", "").toStdString();
	skin.folder = config.getValue("skinFolder", "").toStdString();

	if (skin.folder.empty())
	{
		// if the skin file points to a legacy skin, let it point to the rml skin instead
		if (baseLib::filesystem::hasExtension(skin.filename, ".json"))
		{
			skin.filename = baseLib::filesystem::stripExtension(skin.filename) + ".rml";
			config.setValue("skinFile", juce::String::fromUTF8(skin.filename.c_str()));
		}

		// if the skin is embedded, we do not know the files that this skin was made of. Find them
		for (const auto & s : m_includedSkins)
		{
			if (s.filename == skin.filename)
				skin.files = s.files;
		}
	}

	// do not load legacy skins automatically anymore, revert to default skin
	if (!skin.folder.empty() && baseLib::filesystem::hasExtension(skin.filename, ".json"))
		skin = {};

	return skin;
}

void PluginEditorState::writeSkinToConfig(const Skin& _skin) const
{
	auto& config = m_processor.getConfig();

	config.setValue("skinDisplayName", _skin.displayName.c_str());
	config.setValue("skinFile", juce::String::fromUTF8(_skin.filename.c_str()));
	config.setValue("skinFolder", juce::String::fromUTF8(_skin.folder.c_str()));
}

}
