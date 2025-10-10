#include "jeEditor.h"

#include <juceRmlPlugin/skinConverter/skinConverterOptions.h>

#include "jeAssign.h"
#include "jeController.h"
#include "jeFocusedParameter.h"
#include "jeLcd.h"
#include "jePartButton.h"
#include "jePartSelect.h"
#include "jePatchManager.h"

#include "baseLib/filesystem.h"

#include "jeLib/romloader.h"

#include "jucePluginEditorLib/focusedParameter.h"
#include "jucePluginEditorLib/midiPorts.h"
#include "jucePluginEditorLib/pluginDataModel.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceRmlUi/rmlElemComboBox.h"

#include "RmlUi/Core/ElementDocument.h"

namespace jeJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, const jucePluginEditorLib::Skin& _skin)
		: jucePluginEditorLib::Editor(_processor, _skin)
		, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	{
	}
	void Editor::create()
	{
		jucePluginEditorLib::Editor::create();

		if(auto* romSelector = findChild<juceRmlUi::ElemComboBox>("romselector"))
		{
			const auto rom = jeLib::RomLoader::findROM();

			if(rom.isValid())
			{
				const auto& name = rom.getName();
				romSelector->addOption(name);
			}
			else
			{
				romSelector->addOption("<No ROM found>");
			}
			romSelector->setValue(0);
			romSelector->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
		}

		if (auto* lcd = findChild("lcd"))
			m_lcd.reset(new JeLcd(*this, lcd));

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		onPartChanged.set(m_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
		});

		if (m_lcd)
			m_focusedParameter.reset(new JeFocusedParameter(*this, *m_lcd));
		else
			m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(m_controller, *this));

		m_partSelect.reset(new PartSelect(*this));

		const auto writeButtons = findChildren("btWrite");

		for (auto* writeButton : writeButtons)
		{
			m_writeButtons.emplace_back(std::make_unique<JePartButton>(writeButton, *this));
			m_writeButtons.back()->initalize(static_cast<uint8_t>(JePart::Performance));

			juceRmlUi::EventListener::Add(writeButton, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				onBtWrite(_event);
			});
		}

		m_assign.reset(new JeAssign(*this));
	}

	void Editor::initPluginDataModel(jucePluginEditorLib::PluginDataModel& _model)
	{
		jucePluginEditorLib::Editor::initPluginDataModel(_model);

		// TODO: depends on ROM
		_model.set("deviceModel", "jp8000");
	}

	Editor::~Editor()
	{
		m_assign.reset();
		m_partSelect.reset();
		m_midiPorts.reset();
		m_focusedParameter.reset();
		m_lcd.reset();
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

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}

	void Editor::onBtWrite(Rml::Event& _event) const
	{
		auto* pm = getPatchManager();
		if (!pm)
			return;

		_event.StopPropagation();

		juceRmlUi::Menu menu;

		juceRmlUi::Menu menuLower;
		juceRmlUi::Menu menuUpper;
		juceRmlUi::Menu menuPerformance;

		const auto numUpper = pm->createSaveMenuEntries(menuUpper, 0, "Patch Upper", 0);
		const auto numLower = pm->createSaveMenuEntries(menuLower, 1, "Patch Lower", 1);
		const auto numPerformance = pm->createSaveMenuEntries(menuPerformance, 0, "Patch Lower", 2);

		if (numUpper)
			menu.addSubMenu("Patch Upper", std::move(menuUpper));
		if (numLower)
			menu.addSubMenu("Patch Lower", std::move(menuLower));
		if (numPerformance)
			menu.addSubMenu("Performance", std::move(menuPerformance));

		menu.runModal(_event);
	}
}
