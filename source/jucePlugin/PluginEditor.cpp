#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "VirusController.h"

#include "ui3/VirusEditor.h"

#include "../synthLib/os.h"

//==============================================================================
AudioPluginAudioProcessorEditor::AudioPluginAudioProcessorEditor(AudioPluginAudioProcessor &p) :
	AudioProcessorEditor(&p), processorRef(p), m_parameterBinding(p)
{
	m_includedSkins.push_back({"TI Trancy", "VirusTI_Trancy.json", ""});

	addMouseListener(this, true);

	const auto config = processorRef.getController().getConfig();
    const auto scale = config->getIntValue("scale", 100);
    
	Skin skin = readSkinFromConfig();

	if(skin.jsonFilename.empty())
	{
		skin = m_includedSkins[0];
	}

	loadSkin(skin);
	setGuiScale(scale);

	const auto latencyBlocks = config->getIntValue("latencyBlocks", static_cast<int>(processorRef.getPlugin().getLatencyBlocks()));
	setLatencyBlocks(latencyBlocks);
}

void AudioPluginAudioProcessorEditor::loadSkin(const Skin& _skin)
{
	if(m_currentSkin == _skin)
		return;

	m_currentSkin = _skin;
	writeSkinToConfig(_skin);

	if (m_virusEditor)
	{
		m_parameterBinding.clearBindings();

		if(getIndexOfChildComponent(m_virusEditor.get()) > -1)
			removeChildComponent(m_virusEditor.get());
		m_virusEditor.reset();
	}

	m_rootScale = 1.0f;

	try
	{
		auto* editor = new genericVirusUI::VirusEditor(m_parameterBinding, processorRef, _skin.jsonFilename, _skin.folder, [this] { openMenu(); });
		m_virusEditor.reset(editor);
		setSize(m_virusEditor->getWidth(), m_virusEditor->getHeight());
		m_rootScale = editor->getScale();

		m_virusEditor->setTopLeftPosition(0, 0);
		addAndMakeVisible(m_virusEditor.get());
	}
	catch(const std::runtime_error& _err)
	{
		LOG("ERROR: Failed to create editor: " << _err.what());

		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Skin load failed", _err.what(), "OK");
		m_virusEditor.reset();

		loadSkin(m_includedSkins[0]);
	}
}

void AudioPluginAudioProcessorEditor::setGuiScale(int percent)
{
	setScaleFactor(static_cast<float>(percent)/100.0f * m_rootScale);
	auto* config = processorRef.getController().getConfig();
	config->setValue("scale", percent);
	config->saveIfNeeded();
}

void AudioPluginAudioProcessorEditor::openMenu()
{
	const auto config = processorRef.getController().getConfig();
    const auto scale = config->getIntValue("scale", 100);

	juce::PopupMenu menu;

	juce::PopupMenu skinMenu;

	auto addSkinEntry = [this, &skinMenu](const Skin& _skin)
	{
		skinMenu.addItem(_skin.displayName, true, _skin == m_currentSkin,[this, _skin] {loadSkin(_skin);});
	};

	for (const auto & skin : m_includedSkins)
		addSkinEntry(skin);

	bool haveSkinsOnDisk = false;

	// find more skins on disk
	const auto modulePath = synthLib::getModulePath();

	std::vector<std::string> entries;
	synthLib::getDirectoryEntries(entries, modulePath + "skins");

	for (const auto& entry : entries)
	{
		std::vector<std::string> files;
		synthLib::getDirectoryEntries(files, entry);

		for (const auto& file : files)
		{
			if(synthLib::hasExtension(file, ".json"))
			{
				if(!haveSkinsOnDisk)
				{
					haveSkinsOnDisk = true;
					skinMenu.addSeparator();
				}

				const auto relativePath = entry.substr(modulePath.size());
				auto jsonName = file;
				const auto pathEndPos = jsonName.find_last_of("/\\");
				if(pathEndPos != std::string::npos)
					jsonName = file.substr(pathEndPos+1);
				const Skin skin{jsonName + " (" + relativePath + ")", jsonName, relativePath};
				addSkinEntry(skin);
			}
		}
	}

	if(m_virusEditor && m_currentSkin.folder.empty())
	{
		auto* editor = dynamic_cast<genericVirusUI::VirusEditor*>(m_virusEditor.get());
		if(editor)
		{
			skinMenu.addSeparator();
			skinMenu.addItem("Export current skin to 'skins' folder on disk", true, false, [this]{exportCurrentSkin();});
		}
	}

	juce::PopupMenu scaleMenu;
	scaleMenu.addItem("50%", true, scale == 50, [this] { setGuiScale(50); });
	scaleMenu.addItem("75%", true, scale == 75, [this] { setGuiScale(75); });
	scaleMenu.addItem("100%", true, scale == 100, [this] { setGuiScale(100); });
	scaleMenu.addItem("125%", true, scale == 125, [this] { setGuiScale(125); });
	scaleMenu.addItem("150%", true, scale == 150, [this] { setGuiScale(150); });
	scaleMenu.addItem("200%", true, scale == 200, [this] { setGuiScale(200); });
	scaleMenu.addItem("300%", true, scale == 300, [this] { setGuiScale(300); });

	const auto latency = processorRef.getPlugin().getLatencyBlocks();
	juce::PopupMenu latencyMenu;
	latencyMenu.addItem("0 (DAW will report proper CPU usage)", true, latency == 0, [this] { setLatencyBlocks(0); });
	latencyMenu.addItem("1 (default)", true, latency == 1, [this] { setLatencyBlocks(1); });
	latencyMenu.addItem("2", true, latency == 2, [this] { setLatencyBlocks(2); });
	latencyMenu.addItem("4", true, latency == 4, [this] { setLatencyBlocks(4); });
	latencyMenu.addItem("8", true, latency == 8, [this] { setLatencyBlocks(8); });

	menu.addSubMenu("GUI Skin", skinMenu);
	menu.addSubMenu("GUI Scale", scaleMenu);
	menu.addSubMenu("Latency (blocks)", latencyMenu);

	menu.showMenuAsync(juce::PopupMenu::Options());
}

void AudioPluginAudioProcessorEditor::exportCurrentSkin() const
{
	if(!m_virusEditor)
		return;

	auto* editor = dynamic_cast<genericVirusUI::VirusEditor*>(m_virusEditor.get());

	if(!editor)
		return;

	const auto res = editor->exportToFolder(synthLib::getModulePath() + "skins/");

	if(!res.empty())
	{
		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export failed", "Failed to export skin:\n\n" + res, "OK", m_virusEditor.get());
	}
	else
	{
		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export finished", "Skin successfully exported");
	}
}

AudioPluginAudioProcessorEditor::Skin AudioPluginAudioProcessorEditor::readSkinFromConfig() const
{
	const auto* config = processorRef.getController().getConfig();

	Skin skin;
	skin.displayName = config->getValue("skinDisplayName", "").toStdString();
	skin.jsonFilename = config->getValue("skinFile", "").toStdString();
	skin.folder = config->getValue("skinFolder", "").toStdString();
	return skin;
}

void AudioPluginAudioProcessorEditor::writeSkinToConfig(const Skin& _skin) const
{
	auto* config = processorRef.getController().getConfig();

	config->setValue("skinDisplayName", _skin.displayName.c_str());
	config->setValue("skinFile", _skin.jsonFilename.c_str());
	config->setValue("skinFolder", _skin.folder.c_str());
}

void AudioPluginAudioProcessorEditor::setLatencyBlocks(uint32_t _blocks) const
{
	auto& p = processorRef.getPlugin();

	if(p.setLatencyBlocks(_blocks))
	{
		processorRef.updateLatencySamples();

		auto* config = processorRef.getController().getConfig();
		config->setValue("latencyBlocks", static_cast<int>(_blocks));
		config->saveIfNeeded();
	}
}

AudioPluginAudioProcessorEditor::~AudioPluginAudioProcessorEditor()
{
	m_virusEditor.reset();
}

//==============================================================================
void AudioPluginAudioProcessorEditor::paint (juce::Graphics& g)
{
}

void AudioPluginAudioProcessorEditor::timerCallback()
{
	// ugly (polling!) way for refreshing presets names as this is temporary ui
}

void AudioPluginAudioProcessorEditor::resized()
{
	// This is generally where you'll want to lay out the positions of any
	// subcomponents in your editor..
	auto area = getLocalBounds();
	area.removeFromTop(35);
}

void AudioPluginAudioProcessorEditor::mouseDown(const juce::MouseEvent& event)
{
	if(!event.mods.isPopupMenu())
	{
		AudioProcessorEditor::mouseDown(event);
		return;
	}

	// file browsers have their own menu, do not display two menus at once
	if(event.eventComponent && event.eventComponent->findParentComponentOfClass<juce::FileBrowserComponent>())
		return;

	openMenu();
}
