#include "n2xEditor.h"

#include <juceRmlPlugin/skinConverter/skinConverterOptions.h>

#include "n2xArp.h"
#include "n2xController.h"
#include "n2xFileType.h"
#include "n2xFocusedParameter.h"
#include "n2xLcd.h"
#include "n2xLfo.h"
#include "n2xMasterVolume.h"
#include "n2xOctLed.h"
#include "n2xOutputMode.h"
#include "n2xParts.h"
#include "n2xPatchManager.h"
#include "n2xPluginProcessor.h"
#include "n2xVmMap.h"
#include "baseLib/filesystem.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "jucePluginLib/pluginVersion.h"
#include "juceRmlUi/rmlElemComboBox.h"

#include "n2xLib/n2xdevice.h"
#include "n2xLib/n2xromloader.h"

#include "RmlUi/Core/ElementDocument.h"

namespace n2xJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
	}
	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		if(auto* versionNumber = findChild("VersionNumber", false))
		{
			versionNumber->SetInnerRML(Rml::StringUtilities::EncodeRml(pluginLib::Version::getVersionString()));
		}

		if(auto* romSelector = findChild<juceRmlUi::ElemComboBox>("RomSelector"))
		{
			const auto rom = n2x::RomLoader::findROM();

			if(rom.isValid())
			{
				const auto name = baseLib::filesystem::getFilenameWithoutPath(rom.getFilename());
				romSelector->addOption(name);
			}
			else
			{
				romSelector->addOption("<No ROM found>");
			}
			romSelector->setValue(0);
			romSelector->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		}

		m_arp.reset(new Arp(*this));
		m_focusedParameter.reset(new FocusedParameter(*this));
		m_lcd.reset(new Lcd(*this));
		for(uint8_t i=0; i<m_lfos.size(); ++i)
			m_lfos[i].reset(new Lfo(*this, i));
		m_masterVolume.reset(new MasterVolume(*this));
		m_octLed.reset(new OctLed(*this));
		m_outputMode.reset(new OutputMode(*this));
		m_parts.reset(new Parts(*this));
		m_vmMap.reset(new VmMap(*this));
		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		onPartChanged.set(m_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
		});

		if(auto* bt = findChild("button_store"))
		{
			juceRmlUi::EventListener::Add(bt, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				onBtSave(_event);
			});
		}

		if(auto* bt = findChild("PresetPrev"))
		{
			juceRmlUi::EventListener::Add(bt, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				onBtPrev(_event);
			});
		}

		if(auto* bt = findChild("PresetNext"))
		{
			juceRmlUi::EventListener::Add(bt, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				onBtNext(_event);
			});
		}

		m_onSelectedPatchChanged.set(getPatchManager()->onSelectedPatchChanged, [this](const uint32_t& _part, const pluginLib::patchDB::PatchKey& _patchKey)
		{
			onSelectedPatchChanged(static_cast<uint8_t>(_part), _patchKey);
		});

		Rml::ElementDocument* doc = getDocument();

		juceRmlUi::EventListener::Add(doc, Rml::EventId::Keydown, [this](const Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyModShift(_event);
			getVmMap().setEnabled(key);
		}, false);

		juceRmlUi::EventListener::Add(doc, Rml::EventId::Keyup, [this](const Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyModShift(_event);
			getVmMap().setEnabled(key);
		}, false);
	}

	jucePluginEditorLib::patchManager::PatchManager* Editor::createPatchManager(Rml::Element* _parent)
	{
		return new PatchManager(*this, _parent);
	}

	void Editor::initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions& _skinConverterOptions)
	{
		jucePluginEditorLib::Editor::initSkinConverterOptions(_skinConverterOptions);

		_skinConverterOptions.idReplacements.insert({"ContainerPatchManager", "container-patchmanager"});
	}

	Editor::~Editor()
	{
		m_arp.reset();
		m_focusedParameter.reset();
		m_lcd.reset();
		for (auto& lfo : m_lfos)
			lfo.reset();
		m_masterVolume.reset();
		m_octLed.reset();
		m_outputMode.reset();
		m_parts.reset();
		m_vmMap.reset();
		m_midiPorts.reset();
		m_onSelectedPatchChanged.reset();
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}

	std::string Editor::getCurrentPatchName() const
	{
		const auto part = m_controller.getCurrentPart();
		if(!m_activePatchNames[part].empty())
			return m_activePatchNames[part];
		return m_controller.getPatchName(part);
	}

	void Editor::onPatchActivated(const pluginLib::patchDB::PatchPtr& _patch, const uint32_t _part)
	{
		const auto isMulti = _patch->sysex.size() == n2x::g_multiDumpSize;

		const auto name = _patch->getName();

		setCurrentPatchName(_part, name);

		if(isMulti)
		{
			for (uint8_t p=0; p<m_activePatchNames.size(); ++p)
			{
				if(p == _part)
					continue;

				// set patch name for all parts if the source is a multi
				setCurrentPatchName(p, name);

				// set patch manager selection so that all parts have the multi selected
				getPatchManager()->setSelectedPatch(p, _patch);
			}
		}

		m_lcd->updatePatchName();
	}

	void Editor::createExportFileTypeMenu(juceRmlUi::Menu& _menu, const std::function<void(pluginLib::FileType)>& _func) const
	{
		_menu.addEntry(".nl2", [this, _func]{_func(fileType::g_nl2);});
		jucePluginEditorLib::Editor::createExportFileTypeMenu(_menu, _func);
	}

	void Editor::onBtSave(Rml::Event& _event) const
	{
		juceRmlUi::Menu menu;

		if(getPatchManager()->createSaveMenuEntries(menu, "Program"))
			menu.addSeparator();

		getPatchManager()->createSaveMenuEntries(menu, "Performance", 1);

		menu.runModal(_event);
	}

	void Editor::onBtPrev(Rml::Event& _event) const
	{
		getPatchManager()->selectPrevPreset(m_controller.getCurrentPart());
	}

	void Editor::onBtNext(Rml::Event& _event) const
	{
		getPatchManager()->selectNextPreset(m_controller.getCurrentPart());
	}

	void Editor::setCurrentPatchName(uint8_t _part, const std::string& _name)
	{
		if(m_activePatchNames[_part] == _name)
			return;

		m_activePatchNames[_part] = _name;

		if(m_controller.getCurrentPart() == _part)
			m_lcd->updatePatchName();
	}

	void Editor::onSelectedPatchChanged(uint8_t _part, const pluginLib::patchDB::PatchKey& _patchKey)
	{
		auto source = _patchKey.source;
		if(!source)
			return;

		if(source->patches.empty())
			source = getPatchManager()->getDataSource(*source);

		if(const auto patch = source->getPatch(_patchKey))
			setCurrentPatchName(_part, patch->getName());
	}
}
