#include "n2xEditor.h"

#include "n2xArp.h"
#include "n2xController.h"
#include "n2xFocusedParameter.h"
#include "n2xLcd.h"
#include "n2xLfo.h"
#include "n2xMasterVolume.h"
#include "n2xOctLed.h"
#include "n2xOutputMode.h"
#include "n2xPart.h"
#include "n2xParts.h"
#include "n2xPatchManager.h"
#include "n2xPluginProcessor.h"
#include "n2xVmMap.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "jucePluginLib/parameterbinding.h"
#include "jucePluginLib/pluginVersion.h"

#include "n2xLib/n2xdevice.h"
#include "n2xLib/n2xromloader.h"

namespace n2xJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename)
	: jucePluginEditorLib::Editor(_processor, _binding, std::move(_skinFolder))
	, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	, m_parameterBinding(_binding)
	{
		create(_jsonFilename);

		addMouseListener(this, true);
		{
			// Init Patch Manager
			const auto container = findComponent("ContainerPatchManager");
			constexpr auto scale = 0.8f;
			const float x = static_cast<float>(container->getX());
			const float y = static_cast<float>(container->getY());
			const float w = static_cast<float>(container->getWidth());
			const float h = static_cast<float>(container->getHeight());
			container->setTransform(juce::AffineTransform::scale(scale, scale));
			container->setSize(static_cast<int>(w / scale),static_cast<int>(h / scale));
			container->setTopLeftPosition(static_cast<int>(x / scale),static_cast<int>(y / scale));

			const auto configOptions = getProcessor().getConfigOptions();
			const auto dir = configOptions.getDefaultFile().getParentDirectory();

			setPatchManager(new PatchManager(*this, container, dir));
		}

		if(auto* versionNumber = findComponentT<juce::Label>("VersionNumber", false))
		{
			versionNumber->setText(pluginLib::Version::getVersionString(), juce::dontSendNotification);
		}

		if(auto* romSelector = findComponentT<juce::ComboBox>("RomSelector"))
		{
			const auto rom = n2x::RomLoader::findROM();

			if(rom.isValid())
			{
				constexpr int id = 1;

				const auto name = juce::File(rom.getFilename()).getFileName();
				romSelector->addItem(name, id);
			}
			else
			{
				romSelector->addItem("<No ROM found>", 1);
			}
			romSelector->setSelectedId(1);
			romSelector->setInterceptsMouseClicks(false, false);
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
		m_vmMap.reset(new VmMap(*this, m_parameterBinding));
		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		onPartChanged.set(m_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
			m_parameterBinding.setPart(_part);
		});

		if(auto* bt = findComponentT<juce::Button>("button_store"))
		{
			bt->onClick = [this]
			{
				onBtSave();
			};
		}

		if(auto* bt = findComponentT<juce::Button>("PresetPrev"))
		{
			bt->onClick = [this]
			{
				onBtPrev();
			};
		}

		if(auto* bt = findComponentT<juce::Button>("PresetNext"))
		{
			bt->onClick = [this]
			{
				onBtNext();
			};
		}

		m_onSelectedPatchChanged.set(getPatchManager()->onSelectedPatchChanged, [this](const uint32_t& _part, const pluginLib::patchDB::PatchKey& _patchKey)
		{
			onSelectedPatchChanged(static_cast<uint8_t>(_part), _patchKey);
		});
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

	genericUI::Button<juce::DrawableButton>* Editor::createJuceComponent(genericUI::Button<juce::DrawableButton>* _button, genericUI::UiObject& _object, const std::string& _name, const juce::DrawableButton::ButtonStyle _buttonStyle)
	{
		if(	_name == "PerfSlotActiveA" || 
			_name == "PerfSlotActiveB" || 
			_name == "PerfSlotActiveC" || 
			_name == "PerfSlotActiveD")
			return new Part(*this, _name, _buttonStyle);

		return jucePluginEditorLib::Editor::createJuceComponent(_button, _object, _name, _buttonStyle);
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

	void Editor::mouseEnter(const juce::MouseEvent& _ev)
	{
		m_focusedParameter->onMouseEnter(_ev);
	}

	void Editor::onBtSave() const
	{
		juce::PopupMenu menu;

		if(getPatchManager()->createSaveMenuEntries(menu, "Program"))
			menu.addSeparator();

		getPatchManager()->createSaveMenuEntries(menu, "Performance", 1);

		menu.showMenuAsync({});
	}

	void Editor::onBtPrev() const
	{
		getPatchManager()->selectPrevPreset(m_controller.getCurrentPart());
	}

	void Editor::onBtNext() const
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
