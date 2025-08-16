#include "pluginEditorState.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "baseLib/filesystem.h"

#include "patchmanager/patchmanager.h"

#include "juceUiLib/editor.h"
#include "juceUiLib/messageBox.h"

#include "dsp56kEmu/logging.h"

#include "synthLib/os.h"

namespace jucePluginEditorLib

{
bridgeLib::PluginDesc getPluginDesc(const pluginLib::Processor& _p)
{
	bridgeLib::PluginDesc pd;
	_p.getPluginDesc(pd);
	return pd;
}

PluginEditorState::PluginEditorState(Processor& _processor, pluginLib::Controller& _controller, std::vector<Skin> _includedSkins)
	: m_processor(_processor), m_parameterBinding(_controller), m_includedSkins(std::move(_includedSkins))
	, m_remoteServerList(getPluginDesc(_processor))
{
	juce::File(getSkinFolder()).createDirectory();

	// point embedded skins to public data folder if they're not embedded
	for (auto& skin : m_includedSkins)
	{
		if(skin.folder.empty() && !m_processor.findResource(skin.jsonFilename))
			skin.folder = baseLib::filesystem::validatePath(getSkinFolder() + skin.displayName);
	}
}

PluginEditorState::~PluginEditorState()
{
}

int PluginEditorState::getWidth() const
{
	return m_editor ? m_editor->getWidth() : 0;
}

int PluginEditorState::getHeight() const
{
	return m_editor ? m_editor->getHeight() : 0;
}

const std::vector<Skin>& PluginEditorState::getIncludedSkins()
{
	return m_includedSkins;
}

juce::Component* PluginEditorState::getUiRoot() const
{
	return m_editor.get();
}

void PluginEditorState::disableBindings()
{
	m_parameterBinding.disableBindings();
}

void PluginEditorState::enableBindings()
{
	m_parameterBinding.enableBindings();
}

void PluginEditorState::loadDefaultSkin()
{
	Skin skin = readSkinFromConfig();

	if(skin.jsonFilename.empty())
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
	return baseLib::filesystem::validatePath(m_processor.getDataFolder() + "skins/");
}

bool PluginEditorState::loadSkin(const Skin& _skin, const uint32_t _fallbackIndex/* = 0*/)
{
	auto skin = _skin;

	if (m_editor)
	{
		m_instanceConfig.clear();
		getEditor()->getPerInstanceConfig(m_instanceConfig);

		m_parameterBinding.clearBindings();

		auto* parent = m_editor->getParentComponent();

		if(parent && parent->getIndexOfChildComponent(m_editor.get()) > -1)
			parent->removeChildComponent(m_editor.get());
		m_editor.reset();
	}

	m_rootScale = 1.0f;

	try
	{
		// if the embedded skin cannot be found, use skin folder as fallback
		if(skin.folder.empty() && !m_processor.findResource(skin.jsonFilename))
		{
			skin.folder = baseLib::filesystem::validatePath(getSkinFolder() + skin.displayName);
		}

		auto* editor = createEditor(skin);
		m_editor.reset(editor);

		getEditor()->onOpenMenu.addListener([this](Editor*, const Rml::Event& _event)
		{
			openMenu(_event);
		});

		m_rootScale = editor->getScale();

		m_editor->setTopLeftPosition(0, 0);

		m_currentSkin = skin;
		writeSkinToConfig(skin);

		if(evSkinLoaded)
			evSkinLoaded(m_editor.get());

		if(!m_instanceConfig.empty())
			getEditor()->setPerInstanceConfig(m_instanceConfig);

		return true;
	}
	catch(const std::runtime_error& _err)
	{
		LOG("ERROR: Failed to create editor: " << _err.what());

		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, m_processor.getProperties().name + " - Skin load failed", _err.what());

		m_parameterBinding.clear();
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

Editor* PluginEditorState::getEditor() const
{
	return dynamic_cast<Editor*>(m_editor.get());
}

void PluginEditorState::openMenu(const Rml::Event& _event)
{
	if(getEditor()->openContextMenuForParameter(_event))
		return;

	const auto& config = m_processor.getConfig();
    const auto scale = juce::roundToInt(config.getDoubleValue("scale", 100));

	juceRmlUi::Menu menu;

	juceRmlUi::Menu skinMenu;

	bool loadedSkinIsPartOfList = false;

	std::set<std::pair<std::string, std::string>> knownSkins;	// folder, jsonFilename

	auto addSkinEntry = [this, &skinMenu, &loadedSkinIsPartOfList, &knownSkins](const Skin& _skin)
	{
		// remove dupes by folder
		if(!_skin.folder.empty() && !knownSkins.insert({_skin.folder, _skin.jsonFilename}).second)
			return;

		const auto isCurrent = _skin == getCurrentSkin();

		if(isCurrent)
			loadedSkinIsPartOfList = true;

		skinMenu.addEntry(_skin.displayName, isCurrent,[this, _skin]
		{
			juce::MessageManager::callAsync([this, _skin]
			{
				loadSkin(_skin);
			});
		});
	};

	for (const auto & skin : getIncludedSkins())
		addSkinEntry(skin);

	bool haveSkinsOnDisk = false;

	// find more skins on disk
	const auto modulePath = synthLib::getModulePath();

	// new: user documents folder
	std::vector<std::string> entries;
	baseLib::filesystem::getDirectoryEntries(entries, getSkinFolder());

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
			if(baseLib::filesystem::hasExtension(file, ".json"))
			{
				if(!haveSkinsOnDisk)
				{
					haveSkinsOnDisk = true;
					skinMenu.addSeparator();
				}

				std::string skinPath = entry;
				if(entry.find(modulePath) == 0)
					skinPath = entry.substr(modulePath.size());
				skinPath = baseLib::filesystem::validatePath(skinPath);

				auto jsonName = file;
				const auto pathEndPos = jsonName.find_last_of("/\\");
				if(pathEndPos != std::string::npos)
					jsonName = file.substr(pathEndPos+1);

				const Skin skin{jsonName.substr(0, jsonName.length() - 5), jsonName, skinPath};

				addSkinEntry(skin);
			}
		}
	}

	if(!loadedSkinIsPartOfList)
		addSkinEntry(getCurrentSkin());

	skinMenu.addSeparator();

	if(getEditor() && m_currentSkin.folder.empty() || m_currentSkin.folder.find(getSkinFolder()) != 0)
	{
		skinMenu.addEntry("Export current skin to folder '" + getSkinFolder() + "' on disk", [this]
		{
			exportCurrentSkin();
		});
	}

	skinMenu.addEntry("Open folder '" + getSkinFolder() + "' in File Browser", [this]
	{
		const auto dir = getSkinFolder();
		baseLib::filesystem::createDirectory(dir);
		juce::File(dir).revealToUser();
	});

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

	auto adjustLatency = [this](const int _blocks)
	{
		m_processor.setLatencyBlocks(_blocks);

		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Warning",
			"Most hosts cannot handle if a plugin changes its latency while being in use.\n"
			"It is advised to save, close & reopen the project to prevent synchronization issues.");
	};

	const auto latency = m_processor.getPlugin().getLatencyBlocks();
	juceRmlUi::Menu latencyMenu;
	latencyMenu.addEntry("0 (DAW will report proper CPU usage)", latency == 0, [this, adjustLatency] { adjustLatency(0); });
	latencyMenu.addEntry("1 (default)", latency == 1, [this, adjustLatency] { adjustLatency(1); });
	latencyMenu.addEntry("2", latency == 2, [this, adjustLatency] { adjustLatency(2); });
	latencyMenu.addEntry("4", latency == 4, [this, adjustLatency] { adjustLatency(4); });
	latencyMenu.addEntry("8", latency == 8, [this, adjustLatency] { adjustLatency(8); });

	auto servers = m_remoteServerList.getEntries();

	juceRmlUi::Menu deviceTypeMenu;
	deviceTypeMenu.addEntry("Local (default)", m_processor.getDeviceType() == pluginLib::DeviceType::Local, [this] { m_processor.setDeviceType(pluginLib::DeviceType::Local); });

	if(servers.empty())
	{
		deviceTypeMenu.addEntry("- no servers found -", false, false, [this] {});
	}
	else
	{
		for (const auto & server : servers)
		{
			if(server.err.code == bridgeLib::ErrorCode::Ok)
			{
				std::string name = server.host + ':' + std::to_string(server.serverInfo.portTcp);

				const auto isSelected = m_processor.getDeviceType() == pluginLib::DeviceType::Remote && 
					m_processor.getRemoteDeviceHost() == server.host && 
					m_processor.getRemoteDevicePort() == server.serverInfo.portTcp;

				deviceTypeMenu.addEntry(name, isSelected, [this, server]
				{
					m_processor.setRemoteDevice(server.host, server.serverInfo.portTcp);
				});
			}
			else
			{
				std::string name = server.host + " (error " + std::to_string(static_cast<uint32_t>(server.err.code)) + ", " + server.err.msg + ')';
				deviceTypeMenu.addEntry(name, false, false, [this] {});
			}
		}
	}

	menu.addSubMenu("GUI Skin", std::move(skinMenu));
	menu.addSubMenu("GUI Scale", std::move(scaleMenu));
	menu.addSubMenu("Latency (blocks)", std::move(latencyMenu));

	if (m_processor.getConfig().getBoolValue("supportDspBridge", false))
		menu.addSubMenu("Device Type", std::move(deviceTypeMenu));

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

	{
		menu.addSeparator();

		juceRmlUi::Menu panicMenu;

		panicMenu.addEntry("Send 'All Notes Off'", [this]
		{
			for(uint8_t c=0; c<16; ++c)
			{
				synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor, synthLib::M_CONTROLCHANGE + c, synthLib::MC_ALLNOTESOFF);
				m_processor.addMidiEvent(ev);
			}
		});

		panicMenu.addEntry("Send 'Note Off' for every Note", [this]
		{
			for(uint8_t c=0; c<16; ++c)
			{
				for(uint8_t n=0; n<128; ++n)
				{
					synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor, synthLib::M_NOTEOFF + c, n, 64, n * 256);
					m_processor.addMidiEvent(ev);
				}
			}
		});

		panicMenu.addEntry("Reboot Device", [this]
		{
			m_processor.rebootDevice();
		});

		menu.addSubMenu("Panic", std::move(panicMenu));
	}

	if(auto* editor = dynamic_cast<Editor*>(getEditor()))
	{
		menu.addSeparator();

		if(auto* pm = editor->getPatchManager())
		{
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

void PluginEditorState::exportCurrentSkin() const
{
	auto* editor = getEditor();

	if(!editor)
		return;

	const auto res = editor->exportToFolder(getSkinFolder());

	if(!res.empty())
	{
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Export failed", "Failed to export skin:\n\n" + res, editor);
	}
	else
	{
		genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info, "Export finished", "Skin successfully exported", editor);
	}
}

Skin PluginEditorState::readSkinFromConfig() const
{
	const auto& config = m_processor.getConfig();

	Skin skin;
	skin.displayName = config.getValue("skinDisplayName", "").toStdString();
	skin.jsonFilename = config.getValue("skinFile", "").toStdString();
	skin.folder = config.getValue("skinFolder", "").toStdString();
	return skin;
}

void PluginEditorState::writeSkinToConfig(const Skin& _skin) const
{
	auto& config = m_processor.getConfig();

	config.setValue("skinDisplayName", _skin.displayName.c_str());
	config.setValue("skinFile", juce::String::fromUTF8(_skin.jsonFilename.c_str()));
	config.setValue("skinFolder", juce::String::fromUTF8(_skin.folder.c_str()));
}

}
