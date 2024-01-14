#include "pluginEditorState.h"

#include "pluginProcessor.h"
#include "../synthLib/os.h"
#include "dsp56kEmu/logging.h"

#include "../juceUiLib/editor.h"

namespace jucePluginEditorLib
{
PluginEditorState::PluginEditorState(Processor& _processor, pluginLib::Controller& _controller, std::vector<Skin> _includedSkins)
	: m_processor(_processor), m_parameterBinding(_controller), m_includedSkins(std::move(_includedSkins))
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

const std::vector<PluginEditorState::Skin>& PluginEditorState::getIncludedSkins()
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

void PluginEditorState::loadSkin(const Skin& _skin)
{
	if(m_currentSkin == _skin)
		return;

	m_currentSkin = _skin;
	writeSkinToConfig(_skin);

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
		auto* editor = createEditor(_skin, [this] { openMenu(); });
		m_editor.reset(editor);
		m_rootScale = editor->getScale();

		m_editor->setTopLeftPosition(0, 0);

		if(evSkinLoaded)
			evSkinLoaded(m_editor.get());

		if(!m_instanceConfig.empty())
			getEditor()->setPerInstanceConfig(m_instanceConfig);
	}
	catch(const std::runtime_error& _err)
	{
		LOG("ERROR: Failed to create editor: " << _err.what());

		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Skin load failed", _err.what(), "OK");
		m_editor.reset();

		loadSkin(m_includedSkins[0]);
	}
}

void PluginEditorState::setGuiScale(int _scale) const
{
	if(evSetGuiScale)
		evSetGuiScale(_scale);
}

genericUI::Editor* PluginEditorState::getEditor() const
{
	return static_cast<genericUI::Editor*>(m_editor.get());
}

void PluginEditorState::openMenu()
{
	const auto& config = m_processor.getConfig();
    const auto scale = config.getIntValue("scale", 100);

	juce::PopupMenu menu;

	juce::PopupMenu skinMenu;

	auto addSkinEntry = [this, &skinMenu](const PluginEditorState::Skin& _skin)
	{
		skinMenu.addItem(_skin.displayName, true, _skin == getCurrentSkin(),[this, _skin] {loadSkin(_skin);});
	};

	for (const auto & skin : PluginEditorState::getIncludedSkins())
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

	if(m_editor && m_currentSkin.folder.empty())
	{
		auto* editor = m_editor.get();
		if(editor)
		{
			skinMenu.addSeparator();
			skinMenu.addItem("Export current skin to 'skins' folder on disk", true, false, [this]{exportCurrentSkin();});
		}
	}

	juce::PopupMenu scaleMenu;
	scaleMenu.addItem("50%", true, scale == 50, [this] { setGuiScale(50); });
	scaleMenu.addItem("65%", true, scale == 65, [this] { setGuiScale(65); });
	scaleMenu.addItem("75%", true, scale == 75, [this] { setGuiScale(75); });
	scaleMenu.addItem("85%", true, scale == 85, [this] { setGuiScale(85); });
	scaleMenu.addItem("100%", true, scale == 100, [this] { setGuiScale(100); });
	scaleMenu.addItem("125%", true, scale == 125, [this] { setGuiScale(125); });
	scaleMenu.addItem("150%", true, scale == 150, [this] { setGuiScale(150); });
	scaleMenu.addItem("175%", true, scale == 175, [this] { setGuiScale(175); });
	scaleMenu.addItem("200%", true, scale == 200, [this] { setGuiScale(200); });
	scaleMenu.addItem("250%", true, scale == 250, [this] { setGuiScale(250); });
	scaleMenu.addItem("300%", true, scale == 300, [this] { setGuiScale(300); });

	const auto latency = m_processor.getPlugin().getLatencyBlocks();
	juce::PopupMenu latencyMenu;
	latencyMenu.addItem("0 (DAW will report proper CPU usage)", true, latency == 0, [this] { m_processor.setLatencyBlocks(0); });
	latencyMenu.addItem("1 (default)", true, latency == 1, [this] { m_processor.setLatencyBlocks(1); });
	latencyMenu.addItem("2", true, latency == 2, [this] { m_processor.setLatencyBlocks(2); });
	latencyMenu.addItem("4", true, latency == 4, [this] { m_processor.setLatencyBlocks(4); });
	latencyMenu.addItem("8", true, latency == 8, [this] { m_processor.setLatencyBlocks(8); });

	menu.addSubMenu("GUI Skin", skinMenu);
	menu.addSubMenu("GUI Scale", scaleMenu);
	menu.addSubMenu("Latency (blocks)", latencyMenu);

	initContextMenu(menu);

	menu.showMenuAsync(juce::PopupMenu::Options());
}

void PluginEditorState::exportCurrentSkin() const
{
	if(!m_editor)
		return;

	auto* editor = dynamic_cast<genericUI::Editor*>(m_editor.get());

	if(!editor)
		return;

	const auto res = editor->exportToFolder(synthLib::getModulePath() + "skins/");

	if(!res.empty())
	{
		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export failed", "Failed to export skin:\n\n" + res, "OK", m_editor.get());
	}
	else
	{
		juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export finished", "Skin successfully exported");
	}
}

PluginEditorState::Skin PluginEditorState::readSkinFromConfig() const
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
	config.setValue("skinFile", _skin.jsonFilename.c_str());
	config.setValue("skinFolder", _skin.folder.c_str());
}

}
