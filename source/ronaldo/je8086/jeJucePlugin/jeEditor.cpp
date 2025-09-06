#include "jeEditor.h"

#include <juceRmlPlugin/skinConverter/skinConverterOptions.h>

#include "jeController.h"

#include "baseLib/filesystem.h"

#include "jucePluginEditorLib/midiPorts.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#include "jucePluginLib/pluginVersion.h"
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

		if(auto* versionNumber = findChild("VersionNumber", false))
		{
			versionNumber->SetInnerRML(Rml::StringUtilities::EncodeRml(pluginLib::Version::getVersionString()));
		}
		/*
		if(auto* romSelector = findChild<juceRmlUi::ElemComboBox>("RomSelector"))
		{
			const auto rom = je::RomLoader::findROM();

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
		*/

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		onPartChanged.set(m_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
		});
	}

	jucePluginEditorLib::patchManager::PatchManager* Editor::createPatchManager(Rml::Element* _parent)
	{
		return nullptr;//new PatchManager(*this, _parent);
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
