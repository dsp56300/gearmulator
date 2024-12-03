#include "VirusEditor.h"

#include "ArpUserPattern.h"
#include "PartButton.h"

#include "ParameterNames.h"
#include "VirusProcessor.h"
#include "VirusController.h"

#include "jucePluginLib/parameterbinding.h"
#include "jucePluginLib/pluginVersion.h"

#include "jucePluginEditorLib/filetype.h"
#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

#include "synthLib/os.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(pluginLib::ParameterBinding& _binding, virus::VirusProcessor& _processorRef, const jucePluginEditorLib::Skin& _skin) :
		Editor(_processorRef, _binding, _skin),
		m_processor(_processorRef),
		m_parameterBinding(_binding),
		m_romChangedListener(_processorRef.evRomChanged)
	{
		create();

		m_parts.reset(new Parts(*this));
		m_leds.reset(new Leds(*this, _processorRef));

		// be backwards compatible with old skins
		if(getTabGroupCount() == 0)
			m_tabs.reset(new Tabs(*this));

		// be backwards compatible with old skins
		if(getControllerLinkCountRecursive() == 0)
			m_controllerLinks.reset(new ControllerLinks(*this));

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		// be backwards compatible with old skins
		if(!getConditionCountRecursive())
			m_fxPage.reset(new FxPage(*this));

		{
			auto pmParent = findComponent("ContainerPatchManager", false);
			if(!pmParent)
				pmParent = findComponent("page_presets", false);
			if(!pmParent)
				pmParent = findComponent("page_2_browser");
			setPatchManager(new PatchManager(*this, pmParent));
		}

		m_presetName = findComponentT<juce::Label>("PatchName");

		m_focusedParameter.reset(new jucePluginEditorLib::FocusedParameter(getController(), m_parameterBinding, *this));

		m_romSelector = findComponentT<juce::ComboBox>("RomSelector");

		m_playModeSingle = findComponentT<juce::Button>("PlayModeSingle", false);
		m_playModeMulti = findComponentT<juce::Button>("PlayModeMulti", false);

		if(m_playModeSingle && m_playModeMulti)
		{
			m_playModeSingle->onClick = [this]{ if(m_playModeSingle->getToggleState()) setPlayMode(virusLib::PlayMode::PlayModeSingle); };
			m_playModeMulti->onClick = [this]{ if(m_playModeMulti->getToggleState()) setPlayMode(virusLib::PlayMode::PlayModeMulti); };
		}
		else
		{
			m_playModeToggle = findComponentT<juce::Button>("PlayModeToggle");
			m_playModeToggle->onClick = [this]{ setPlayMode(m_playModeToggle->getToggleState() ? virusLib::PlayMode::PlayModeMulti : virusLib::PlayMode::PlayModeSingle); };
		}

		if(m_romSelector)
		{
			const auto roms = m_processor.getRoms();

			if(roms.empty())
			{
				m_romSelector->addItem("<No ROM found>", 1);
			}
			else
			{
				int id = 1;

				for (const auto& rom : roms)
				{
					const auto name = juce::File(rom.getFilename()).getFileNameWithoutExtension();
					m_romSelector->addItem(name + " (" + rom.getModelName() + ')', id++);
				}
			}

			m_romSelector->setSelectedId(static_cast<int>(m_processor.getSelectedRomIndex()) + 1, juce::dontSendNotification);

			m_romSelector->onChange = [this]
			{
				const auto oldIndex = m_processor.getSelectedRomIndex();
				const auto newIndex = m_romSelector->getSelectedId() - 1;
				if(!m_processor.setSelectedRom(newIndex))
					m_romSelector->setSelectedId(static_cast<int>(oldIndex) + 1);
			};
		}

		getController().onProgramChange = [this](int _part) { onProgramChange(_part); };

		addMouseListener(this, true);

		if(auto* versionInfo = findComponentT<juce::Label>("VersionInfo", false))
		{
		    const std::string message = "DSP 56300 Emulator Version " + pluginLib::Version::getVersionString() + " - " + pluginLib::Version::getVersionDateTime();
			versionInfo->setText(message, juce::dontSendNotification);
		}

		if(auto* versionNumber = findComponentT<juce::Label>("VersionNumber", false))
		{
			versionNumber->setText(pluginLib::Version::getVersionString(), juce::dontSendNotification);
		}

		m_deviceModel = findComponentT<juce::Label>("DeviceModel", false);

		auto* presetSave = findComponentT<juce::Button>("PresetSave", false);
		if(presetSave)
			presetSave->onClick = [this] { savePreset(); };

		auto* presetLoad = findComponentT<juce::Button>("PresetLoad", false);
		if(presetLoad)
			presetLoad->onClick = [this] { loadPreset(); };

		m_presetName->setEditable(false, true, true);
		m_presetName->onTextChange = [this]()
		{
			const auto text = m_presetName->getText();
			if (text.trim().length() > 0)
			{
				getController().setSinglePresetName(getController().getCurrentPart(), text);
				onProgramChange(getController().getCurrentPart());
			}
		};

		m_presetNameMouseListener = new PartMouseListener(pluginLib::MidiPacket::AnyPart, [this](const juce::MouseEvent&, int)
		{
			startDragging(new jucePluginEditorLib::patchManager::SavePatchDesc(*getPatchManager(), getController().getCurrentPart()), m_presetName);
		});

		m_presetName->addMouseListener(m_presetNameMouseListener, false);

		auto* menuButton = findComponentT<juce::Button>("Menu", false);

		if(menuButton)
		{
			menuButton->onClick = [this]()
			{
				openMenu(nullptr);
			};
		}

		updatePresetName();
		updatePlayModeButtons();

		m_romChangedListener = [this](auto)
		{
			updateDeviceModel();
			updateKeyValueConditions("deviceModel", virusLib::getModelName(m_processor.getModel()));
			m_parts->onPlayModeChanged();
		};
	}

	VirusEditor::~VirusEditor()
	{
		m_presetName->removeMouseListener(m_presetNameMouseListener);
		delete m_presetNameMouseListener;
		m_presetNameMouseListener = nullptr;

		m_focusedParameter.reset();

		m_parameterBinding.clearBindings();

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

	genericUI::Button<juce::TextButton>* VirusEditor::createJuceComponent(genericUI::Button<juce::TextButton>* _button, genericUI::UiObject& _object)
	{
		if(_object.getName() == "PresetName")
			return new PartButton(*this);

		return Editor::createJuceComponent(_button, _object);
	}

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

	void VirusEditor::mouseEnter(const juce::MouseEvent& event)
	{
		m_focusedParameter->onMouseEnter(event);
	}

	void VirusEditor::updatePresetName() const
	{
		m_presetName->setText(getController().getCurrentPartPresetName(getController().getCurrentPart()), juce::dontSendNotification);
	}

	void VirusEditor::updatePlayModeButtons() const
	{
		if(m_playModeSingle)
			m_playModeSingle->setToggleState(!getController().isMultiMode(), juce::dontSendNotification);
		if(m_playModeMulti)
			m_playModeMulti->setToggleState(getController().isMultiMode(), juce::dontSendNotification);
		if(m_playModeToggle)
			m_playModeToggle->setToggleState(getController().isMultiMode(), juce::dontSendNotification);
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

		m_deviceModel->setText(m, juce::dontSendNotification);
	}

	void VirusEditor::savePreset()
	{
		juce::PopupMenu menu;

		const auto countAdded = getPatchManager()->createSaveMenuEntries(menu);

		if(countAdded)
			menu.addSeparator();

		auto addEntry = [&](juce::PopupMenu& _menu, const std::string& _name, const std::function<void(jucePluginEditorLib::FileType)>& _callback)
		{
			juce::PopupMenu subMenu;

			subMenu.addItem(".syx", [_callback](){_callback(jucePluginEditorLib::FileType::Syx); });
			subMenu.addItem(".mid", [_callback](){_callback(jucePluginEditorLib::FileType::Mid); });

			_menu.addSubMenu(_name, subMenu);
		};

		addEntry(menu, "Export Current Single (Edit Buffer)", [this](const jucePluginEditorLib::FileType& _type)
		{
			savePresets(SaveType::CurrentSingle, _type);
		});

		if(getController().isMultiMode())
		{
			addEntry(menu, "Export Arrangement (Multi + 16 Singles)", [this](const jucePluginEditorLib::FileType& _type)
			{
				savePresets(SaveType::Arrangement, _type);
			});
		}

		juce::PopupMenu banksMenu;
		for(uint8_t b=0; b<static_cast<uint8_t>(getController().getBankCount()); ++b)
		{
			addEntry(banksMenu, getController().getBankName(b), [this, b](const jucePluginEditorLib::FileType& _type)
			{
				savePresets(SaveType::Bank, _type, b);
			});
		}

		menu.addSubMenu("Export Bank", banksMenu);

		menu.showMenuAsync(juce::PopupMenu::Options());
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

						if(1 == juce::NativeMessageBox::showYesNoBox(juce::MessageBoxIconType::QuestionIcon, title, message, nullptr))
						{
							setPlayMode(virusLib::PlayMode::PlayModeMulti);
							c.sendSysEx(multi);

							for(uint8_t i=0; i<static_cast<uint8_t>(singles.size()); ++i)
							{
								const auto& single = singles[i];
								c.modifySingleDump(single, virusLib::BankNumber::EditBuffer, i);
								c.activatePatch(single, i);
							}

							c.requestArrangement();
						}
						return;
					}
				}
				juce::NativeMessageBox::showMessageBox(juce::AlertWindow::InfoIcon, "Information", 
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

	void VirusEditor::savePresets(SaveType _saveType, const jucePluginEditorLib::FileType& _fileType, uint8_t _bankNumber/* = 0*/)
	{
		Editor::savePreset([this, _saveType, _bankNumber, _fileType](const juce::File& _result)
		{
			jucePluginEditorLib::FileType fileType = _fileType;
			const auto file = createValidFilename(fileType, _result);
			savePresets(file, _saveType, fileType, _bankNumber);
		});
	}

	bool VirusEditor::savePresets(const std::string& _pathName, SaveType _saveType, const jucePluginEditorLib::FileType& _fileType, uint8_t _bankNumber/* = 0*/) const
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

	void VirusEditor::setPart(size_t _part)
	{
		m_parameterBinding.setPart(static_cast<uint8_t>(_part));
		onCurrentPartChanged();
		setCurrentPart(static_cast<uint8_t>(_part));
	}
}
