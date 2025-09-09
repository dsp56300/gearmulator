#include "jeEditor.h"

#include <juceRmlPlugin/skinConverter/skinConverterOptions.h>

#include "jeController.h"
#include "jeLcd.h"
#include "jePatchManager.h"

#include "baseLib/filesystem.h"

#include "jeLib/romloader.h"

#include "jucePluginEditorLib/midiPorts.h"
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
			m_lcd.reset(new JeLcd(lcd));

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		onPartChanged.set(m_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
		});
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
		m_midiPorts.reset();
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {};
	}
}
