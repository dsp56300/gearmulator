#include "VirusEditor.h"

#include "ArpUserPattern.h"
#include "ControllerLinks.h"

#include "ParameterNames.h"
#include "VirusProcessor.h"
#include "VirusController.h"

#include "jucePluginLib/filetype.h"
#include "jucePluginLib/parameterbinding.h"
#include "jucePluginLib/pluginVersion.h"

#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"
#include "jucePluginEditorLib/pluginDataModel.h"

#include "juceRmlPlugin/skinConverter/skinConverterOptions.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInplaceEditor.h"

#include "juceUiLib/messageBox.h"

#include "RmlUi/Core/ElementDocument.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(pluginLib::ParameterBinding& _binding, virus::VirusProcessor& _processorRef, const jucePluginEditorLib::Skin& _skin) :
		Editor(_processorRef, _skin),
		m_processor(_processorRef),
		m_romChangedListener(_processorRef.evRomChanged)
	{
		create();

		m_parts.reset(new Parts(*this));
		m_leds.reset(new Leds(*this, _processorRef));

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		m_presetName = findChild("PatchName");

		m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(getController(), *getRmlParameterBinding(), *this));

		m_romSelector = findChild<juceRmlUi::ElemComboBox>("RomSelector");

		m_playModeSingle = findChild<juceRmlUi::ElemButton>("PlayModeSingle", false);
		m_playModeMulti = findChild<juceRmlUi::ElemButton>("PlayModeMulti", false);

		if(m_playModeSingle && m_playModeMulti)
		{
			juceRmlUi::EventListener::AddClick(m_playModeSingle, [this] { setPlayMode(virusLib::PlayMode::PlayModeSingle); });
			juceRmlUi::EventListener::AddClick(m_playModeMulti, [this] { setPlayMode(virusLib::PlayMode::PlayModeMulti); });
		}
		else
		{
			m_playModeToggle = findChild<juceRmlUi::ElemButton>("PlayModeToggle");
			juceRmlUi::EventListener::AddClick(m_playModeToggle, [this] { setPlayMode(m_playModeToggle->isChecked() ? virusLib::PlayMode::PlayModeMulti : virusLib::PlayMode::PlayModeSingle); });
		}

		if(m_romSelector)
		{
			const auto roms = m_processor.getRoms();

			if(roms.empty())
			{
				m_romSelector->addOption("<No ROM found>");
			}
			else
			{
				int id = 1;

				for (const auto& rom : roms)
				{
					const auto name = juce::File(rom.getFilename()).getFileNameWithoutExtension();
					m_romSelector->addOption(name.toStdString() + " (" + rom.getModelName() + ')');
				}
			}

			m_romSelector->setSelectedIndex(static_cast<int>(m_processor.getSelectedRomIndex()), false);

			juceRmlUi::EventListener::Add(m_romSelector, Rml::EventId::Change, [this](Rml::Event&)
			{
				const auto oldIndex = m_processor.getSelectedRomIndex();
				const auto newIndex = m_romSelector->getSelectedIndex();
				if(!m_processor.setSelectedRom(newIndex))
					m_romSelector->setSelectedIndex(static_cast<int>(oldIndex));
			});
		}

		getController().onProgramChange = [this](int _part)
		{
			const juceRmlUi::RmlInterfaces::ScopedAccess access(*getRmlComponent());
			onProgramChange(_part);
		};

		if(auto* versionInfo = findChild("VersionInfo", false))
		{
		    const std::string message = "DSP 56300 Emulator Version " + pluginLib::Version::getVersionString() + " - " + pluginLib::Version::getVersionDateTime();
			versionInfo->SetInnerRML(message);
		}

		if(auto* versionNumber = findChild("VersionNumber", false))
		{
			versionNumber->SetInnerRML(pluginLib::Version::getVersionString());
		}

		m_deviceModel = findChild("DeviceModel", false);

		auto* presetSave = findChild("PresetSave", false);
		if(presetSave)
			juceRmlUi::EventListener::Add(presetSave, Rml::EventId::Click, [this](Rml::Event& _event) { savePreset(_event); });

		auto* presetLoad = findChild("PresetLoad", false);
		if(presetLoad)
			juceRmlUi::EventListener::AddClick(presetLoad, [this] { loadPreset(); });

		juceRmlUi::EventListener::Add(m_presetName, Rml::EventId::Dblclick, [this](Rml::Event& _event)
		{
			new juceRmlUi::InplaceEditor(m_presetName, Rml::StringUtilities::StripWhitespace(m_presetName->GetInnerRML()), [this](const std::string& _text)
			{
				auto text = Rml::StringUtilities::StripWhitespace(_text);
				if (text.empty())
					return;
				getController().setSinglePresetName(getController().getCurrentPart(), text);
				onProgramChange(getController().getCurrentPart());
			});
		});

/*
		m_presetNameMouseListener = new PartMouseListener(pluginLib::MidiPacket::AnyPart, [this](const juce::MouseEvent&, int)
		{
			startDragging(new jucePluginEditorLib::patchManager::SavePatchDesc(*getPatchManager(), getController().getCurrentPart()), m_presetName);
		});
		m_presetName->addMouseListener(m_presetNameMouseListener, false);
*/

		auto* menuButton = findChild("Menu", false);

		if(menuButton)
		{
			juceRmlUi::EventListener::Add(menuButton, Rml::EventId::Click, [this](Rml::Event& _event)
			{
				openMenu(_event);
			});
		}

		updatePresetName();
		updatePlayModeButtons();

		m_romChangedListener = [this](auto)
		{
			updateDeviceModel();
			getPluginDataModel()->set("deviceModel", virusLib::getModelName(m_processor.getModel()));
			m_parts->onPlayModeChanged();
		};

		if (auto* arpUserGraphics = findChild("ArpUserGraphics", false))
			m_arpUserPattern.reset(new ArpUserPattern(*this, arpUserGraphics));

		ControllerLinks::create(*this);
	}

	VirusEditor::~VirusEditor()
	{
		m_arpUserPattern.reset();

		m_focusedParameter.reset();

		getController().onProgramChange = nullptr;
	}

	virus::Controller& VirusEditor::getController() const
	{
		return static_cast<virus::Controller&>(m_processor.getController());
	}

	std::pair<std::string, std::string> VirusEditor::getDemoRestrictionText() const
	{
		return {
				m_processor.getProperties().name + " - Demo Mode",
				m_processor.getProperties().name + " runs in demo mode, the following restrictions apply:\n"
				"\n"
				"* The plugin state is not preserved\n"
				"* Preset saving is disabled"};
	}
	/*
	juce::Component* VirusEditor::createJuceComponent(juce::Component* _component, genericUI::UiObject& _object)
	{
		if(_object.getName() == "ArpUserGraphics")
		{
			assert(m_arpUserPattern == nullptr);
			m_arpUserPattern = new ArpUserPattern(*this);
			return m_arpUserPattern;
		}

		return Editor::createJuceComponent(_component, _object);
	}
	*/
	void VirusEditor::initSkinConverterOptions(rmlPlugin::skinConverter::SkinConverterOptions& _skinConverterOptions)
	{
		Editor::initSkinConverterOptions(_skinConverterOptions);

		_skinConverterOptions.idReplacements.insert({"page_presets", "container-patchmanager"});
		_skinConverterOptions.idReplacements.insert({"ContainerPatchManager", "container-patchmanager"});
		_skinConverterOptions.idReplacements.insert({"page_2_browser", "container-patchmanager"});
	}

	void VirusEditor::initPluginDataModel(jucePluginEditorLib::PluginDataModel& _model)
	{
		Editor::initPluginDataModel(_model);

		_model.set("deviceModel", virusLib::getModelName(m_processor.getModel()));
		_model.setFunc("multiMode", [this]
		{
			return getController().isMultiMode() ? "1" : "0";
		}, [this](const std::string& _multiMode)
		{
			try
			{
				auto mode = std::stoi(_multiMode);
				setPlayMode(mode ? virusLib::PlayModeMulti : virusLib::PlayModeSingle);
			}
			catch (std::exception&)
			{
				// ignore invalid input
			}
		});
	}

	jucePluginEditorLib::patchManager::PatchManager* VirusEditor::createPatchManager(juceRmlUi::RmlComponent& _rmlCompnent, Rml::Element* _parent)
	{
		return new PatchManager(*this, _rmlCompnent, _parent);
	}

	void VirusEditor::onProgramChange(int _part)
	{
		m_parts->onProgramChange();
		updatePresetName();
		updatePlayModeButtons();
		if(getPatchManager())
			getPatchManager()->onProgramChanged(_part);
	}

	void VirusEditor::onPlayModeChanged()
	{
		m_parts->onPlayModeChanged();
		updatePresetName();
		updatePlayModeButtons();
	}

	void VirusEditor::onCurrentPartChanged()
	{
		m_parts->onCurrentPartChanged();
		if(m_arpUserPattern)
			m_arpUserPattern->onCurrentPartChanged();
		updatePresetName();
	}

	void VirusEditor::updatePresetName() const
	{
		m_presetName->SetInnerRML(getController().getCurrentPartPresetName(getController().getCurrentPart()));
	}

	void VirusEditor::updatePlayModeButtons() const
	{
		if(m_playModeSingle)
			m_playModeSingle->setChecked(!getController().isMultiMode());
		if(m_playModeMulti)
			m_playModeMulti->setChecked(getController().isMultiMode());
		if(m_playModeToggle)
			m_playModeToggle->setChecked(getController().isMultiMode());
	}

	void VirusEditor::updateDeviceModel()
	{
		if(!m_deviceModel)
			return;

		std::string m;

		switch(m_processor.getModel())
		{
		case virusLib::DeviceModel::Invalid:
			return;
		case virusLib::DeviceModel::ABC:
			{
				auto* rom = m_processor.getSelectedRom();
				if(!rom)
					return;

				virusLib::ROMFile::TPreset data;
				if(!rom->getSingle(0, 0, data))
					return;

				switch(virusLib::Microcontroller::getPresetVersion(data.front()))
				{
				case virusLib::A:	m = "A";	break;
				case virusLib::B:	m = "B";	break;
				case virusLib::C:	m = "C";	break;
				case virusLib::D:	m = "TI";	break;
				case virusLib::D2:	m = "TI2";	break;
				default:			m = "?";	break;
				}
			}
			break;
		case virusLib::DeviceModel::Snow:	m = "Snow";	break;
		case virusLib::DeviceModel::TI:		m = "TI";	break;
		case virusLib::DeviceModel::TI2:	m = "TI2";	break;
		}

		m_deviceModel->SetInnerRML(m);
	}

	void VirusEditor::savePreset(Rml::Event& _event)
	{
		juceRmlUi::Menu menu;

		const auto countAdded = getPatchManager()->createSaveMenuEntries(menu);

		if(countAdded)
			menu.addSeparator();

		auto addEntry = [&](juceRmlUi::Menu& _menu, const std::string& _name, const std::function<void(pluginLib::FileType)>& _callback)
		{
			juceRmlUi::Menu subMenu;

			subMenu.addEntry(".syx", [_callback](){_callback(pluginLib::FileType::Syx); });
			subMenu.addEntry(".mid", [_callback](){_callback(pluginLib::FileType::Mid); });

			_menu.addSubMenu(_name, std::move(subMenu));
		};

		addEntry(menu, "Export Current Single (Edit Buffer)", [this](const pluginLib::FileType& _type)
		{
			savePresets(SaveType::CurrentSingle, _type);
		});

		if(getController().isMultiMode())
		{
			addEntry(menu, "Export Arrangement (Multi + 16 Singles)", [this](const pluginLib::FileType& _type)
			{
				savePresets(SaveType::Arrangement, _type);
			});
		}

		juceRmlUi::Menu banksMenu;
		for(uint8_t b=0; b<static_cast<uint8_t>(getController().getBankCount()); ++b)
		{
			addEntry(banksMenu, getController().getBankName(b), [this, b](const pluginLib::FileType& _type)
			{
				savePresets(SaveType::Bank, _type, b);
			});
		}

		menu.addSubMenu("Export Bank", std::move(banksMenu));

		menu.runModal(_event);
	}

	void VirusEditor::loadPreset()
	{
		Editor::loadPreset([this](const juce::File& _result)
		{
			pluginLib::patchDB::DataList results;

			if(!getPatchManager()->loadFile(results, _result.getFullPathName().toStdString()))
				return;

			auto& c = getController();

			// we attempt to convert all results as some of them might not be valid preset data
			for(size_t i=0; i<results.size();)
			{
				// convert to load to edit buffer of current part
				const auto data = c.modifySingleDump(results[i], virusLib::BankNumber::EditBuffer, c.isMultiMode() ? c.getCurrentPart() : virusLib::SINGLE);
				if(data.empty())
					results.erase(results.begin() + i);
				else
					results[i++] = data;
			}

			if (results.size() == 1)
			{
				c.activatePatch(results.front());
			}
			else if(results.size() > 1)
			{
				// check if this is one multi and 16 singles and load them as arrangement dump. Ask user for confirmation first
				if(results.size() == 17)
				{
					uint32_t multiCount = 0;

					pluginLib::patchDB::DataList singles;
					pluginLib::patchDB::Data multi;

					for (const auto& result : results)
					{
						if(result.size() < 256)
							continue;

						const auto cmd = result[6];

						if(cmd == virusLib::SysexMessageType::DUMP_MULTI)
						{
							multi = result;
							++multiCount;
						}
						else if(cmd == virusLib::SysexMessageType::DUMP_SINGLE)
						{
							singles.push_back(result);
						}
					}

					if(multiCount == 1 && singles.size() == 16)
					{
						const auto title = m_processor.getProductName(true) + " - Load Arrangement Dump?";
						const auto message = "This file contains an arrangement dump, i.e. one Multi and 16 Singles.\nDo you want to replace the current state by this dump?";

						genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question, title, message, [this, multi, singles](const genericUI::MessageBox::Result _result)
						{
							if (_result != genericUI::MessageBox::Result::Yes)
								return;

							auto& c = getController();

							setPlayMode(virusLib::PlayMode::PlayModeMulti);
							c.sendSysEx(multi);

							for(uint8_t i=0; i<static_cast<uint8_t>(singles.size()); ++i)
							{
								const auto& single = singles[i];
								c.modifySingleDump(single, virusLib::BankNumber::EditBuffer, i);
								c.activatePatch(single, i);
							}

							c.requestArrangement();
						});

						return;
					}
				}
				genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Info, "Information", 
					"The selected file contains more than one patch. Please add this file as a data source in the Patch Manager instead.\n\n"
					"Go to the Patch Manager, right click the 'Data Sources' node and select 'Add File...' to import it."
				);
			}
		});
	}

	void VirusEditor::setPlayMode(uint8_t _playMode)
	{
		const auto playMode = getController().getParameterIndexByName(virus::g_paramPlayMode);

		auto* param = getController().getParameter(playMode);
		param->setUnnormalizedValueNotifyingHost(_playMode, pluginLib::Parameter::Origin::Ui);

		// we send this directly here as we request a new arrangement below, we don't want to wait on juce to inform the knob to have changed
		getController().sendParameterChange(*param, _playMode);

		if (_playMode == virusLib::PlayModeSingle && getController().getCurrentPart() != 0)
			setPart(0);

		onPlayModeChanged();

		getController().requestArrangement();
	}

	void VirusEditor::savePresets(SaveType _saveType, const pluginLib::FileType& _fileType, uint8_t _bankNumber/* = 0*/)
	{
		Editor::savePreset(_fileType, [this, _saveType, _bankNumber, _fileType](const juce::File& _result)
		{
			pluginLib::FileType fileType = _fileType;
			const auto file = createValidFilename(fileType, _result);
			savePresets(file, _saveType, fileType, _bankNumber);
		});
	}

	bool VirusEditor::savePresets(const std::string& _pathName, SaveType _saveType, const pluginLib::FileType& _fileType, uint8_t _bankNumber/* = 0*/) const
	{
#if SYNTHLIB_DEMO_MODE
		return false;
#else
		std::vector< std::vector<uint8_t> > messages;
		
		switch (_saveType)
		{
		case SaveType::CurrentSingle:
			{
				const auto dump = getController().createSingleDump(getController().getCurrentPart(), toMidiByte(virusLib::BankNumber::A), 0);
				messages.push_back(dump);
			}
			break;
		case SaveType::Bank:
			{
				const auto& presets = getController().getSinglePresets();
				if(_bankNumber < presets.size())
				{
					const auto& bankPresets = presets[_bankNumber];
					for (const auto& bankPreset : bankPresets)
						messages.push_back(bankPreset.data);
				}
			}
			break;
		case SaveType::Arrangement:
			{
				getController().onMultiReceived = [this, _fileType, _pathName]
				{
					std::vector< std::vector<uint8_t> > messages;
					messages.push_back(getController().getMultiEditBuffer().data);

					for(uint8_t i=0; i<16; ++i)
					{
						const auto dump = getController().createSingleDump(i, toMidiByte(virusLib::BankNumber::EditBuffer), i);
						messages.push_back(dump);
						Editor::savePresets(_fileType, _pathName, messages);
					}

					getController().onMultiReceived = {};
				};
				getController().requestMulti(0, 0);
			}
			return true;
		default:
			return false;
		}

		return Editor::savePresets(_fileType, _pathName, messages);
#endif
	}

	void VirusEditor::setPart(const size_t _part)
	{
		onCurrentPartChanged();
		setCurrentPart(static_cast<uint8_t>(_part));
	}
}
