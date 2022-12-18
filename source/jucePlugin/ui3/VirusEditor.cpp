#include "VirusEditor.h"

#include "BinaryData.h"

#include "../ParameterNames.h"
#include "../PluginProcessor.h"
#include "../VirusController.h"
#include "../version.h"

#include "../../jucePluginLib/parameterbinding.h"

#include "../../synthLib/os.h"
#include "../../synthLib/sysexToMidi.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(pluginLib::ParameterBinding& _binding, AudioPluginAudioProcessor& _processorRef, const std::string& _jsonFilename, std::string _skinFolder, std::function<void()> _openMenuCallback) :
		Editor(_processorRef, _binding, std::move(_skinFolder), _jsonFilename),
		m_processor(_processorRef),
		m_parameterBinding(_binding),
		m_openMenuCallback(std::move(_openMenuCallback))
	{
		create(_jsonFilename);

		m_parts.reset(new Parts(*this));

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

		m_patchBrowser.reset(new PatchBrowser(*this));

		m_presetName = findComponentT<juce::Label>("PatchName");
		m_focusedParameterName = findComponentT<juce::Label>("FocusedParameterName");
		m_focusedParameterValue = findComponentT<juce::Label>("FocusedParameterValue");
		m_focusedParameterTooltip = findComponentT<juce::Label>("FocusedParameterTooltip", false);

		if(m_focusedParameterTooltip)
			m_focusedParameterTooltip->setVisible(false);

		m_romSelector = findComponentT<juce::ComboBox>("RomSelector");

		m_playModeSingle = findComponentT<juce::Button>("PlayModeSingle", false);
		m_playModeMulti = findComponentT<juce::Button>("PlayModeMulti", false);

		if(m_playModeSingle && m_playModeMulti)
		{
			m_playModeSingle->onClick = [this]{ setPlayMode(virusLib::PlayMode::PlayModeSingle); };
			m_playModeMulti->onClick = [this]{ setPlayMode(virusLib::PlayMode::PlayModeMulti); };
		}
		else
		{
			m_playModeToggle = findComponentT<juce::Button>("PlayModeToggle");
			m_playModeToggle->onClick = [this]{ setPlayMode(m_playModeToggle->getToggleState() ? virusLib::PlayMode::PlayModeMulti : virusLib::PlayMode::PlayModeSingle); };
		}

		if(m_romSelector)
		{
			if(!_processorRef.isPluginValid())
				m_romSelector->addItem("<No ROM found>", 1);
			else
				m_romSelector->addItem(_processorRef.getRomName(), 1);

			m_romSelector->setSelectedId(1, juce::dontSendNotification);
		}

		getController().onProgramChange = [this] { onProgramChange(); };

		addMouseListener(this, true);

		m_focusedParameterName->setVisible(false);
		m_focusedParameterValue->setVisible(false);

		if(auto* versionInfo = findComponentT<juce::Label>("VersionInfo", false))
		{
		    const std::string message = "DSP 56300 Emulator Version " + std::string(g_pluginVersionString) + " - " __DATE__ " " __TIME__;
			versionInfo->setText(message, juce::dontSendNotification);
		}

		if(auto* versionNumber = findComponentT<juce::Label>("VersionNumber", false))
		{
			versionNumber->setText(g_pluginVersionString, juce::dontSendNotification);
		}

		m_deviceModel = findComponentT<juce::Label>("DeviceModel", false);

		updateDeviceModel();

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
				onProgramChange();
			}
		};

		auto* menuButton = findComponentT<juce::Button>("Menu", false);

		if(menuButton)
			menuButton->onClick = m_openMenuCallback;

		updatePresetName();
		updatePlayModeButtons();
		updateControlLabel(nullptr);

		for (auto& params : getController().getExposedParameters())
		{
			for (const auto& param : params.second)
			{
				m_boundParameters.push_back(param);

				param->onValueChanged.emplace_back(1, [this, param]()
				{
					if (param->getChangeOrigin() == pluginLib::Parameter::ChangedBy::PresetChange || 
						param->getChangeOrigin() == pluginLib::Parameter::ChangedBy::Derived)
						return;
					auto* comp = m_parameterBinding.getBoundComponent(param);
					if(comp)
						updateControlLabel(comp);
				});
			}
		}
	}

	VirusEditor::~VirusEditor()
	{
		for (auto* p : m_boundParameters)
			p->removeListener(1);

		m_parameterBinding.clearBindings();

		getController().onProgramChange = nullptr;
	}

	Virus::Controller& VirusEditor::getController() const
	{
		return static_cast<Virus::Controller&>(m_processor.getController());
	}

	const char* VirusEditor::findEmbeddedResource(const std::string& _filename, uint32_t& _size)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_size = static_cast<uint32_t>(size);
			return res;
		}
		return nullptr;
	}

	const char* VirusEditor::findResourceByFilename(const std::string& _filename, uint32_t& _size)
	{
		return findEmbeddedResource(_filename, _size);
	}

	PatchBrowser* VirusEditor::getPatchBrowser()
	{
		return m_patchBrowser.get();
	}

	void VirusEditor::onProgramChange()
	{
		m_parts->onProgramChange();
		updatePresetName();
		updatePlayModeButtons();
		updateDeviceModel();
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
		updatePresetName();
	}

	void VirusEditor::mouseEnter(const juce::MouseEvent& event)
	{
		if(event.eventComponent && event.eventComponent->getProperties().contains("parameter"))
			updateControlLabel(event.eventComponent);
	}

	void VirusEditor::timerCallback()
	{
		updateControlLabel(nullptr);
	}

	void VirusEditor::updateControlLabel(juce::Component* _component)
	{
		stopTimer();

		if(_component)
		{
			// combo boxes report the child label as event source, try the parent in this case
			if(!_component->getProperties().contains("parameter"))
				_component = _component->getParentComponent();
		}

		if(!_component || !_component->getProperties().contains("parameter"))
		{
			m_focusedParameterName->setVisible(false);
			m_focusedParameterValue->setVisible(false);
			if(m_focusedParameterTooltip)
				m_focusedParameterTooltip->setVisible(false);
			return;
		}

		const auto& props = _component->getProperties();
		const int v = props["parameter"];

		const int part = props.contains("part") ? static_cast<int>(props["part"]) : static_cast<int>(getController().getCurrentPart());

		const auto* p = getController().getParameter(v, part);

		if(!p)
		{
			m_focusedParameterName->setVisible(false);
			m_focusedParameterValue->setVisible(false);
			if(m_focusedParameterTooltip)
				m_focusedParameterTooltip->setVisible(false);
			return;
		}

		const auto value = p->getText(p->getValue(), 0);

		const auto& desc = p->getDescription();

		m_focusedParameterName->setText(desc.displayName, juce::dontSendNotification);
		m_focusedParameterValue->setText(value, juce::dontSendNotification);

		m_focusedParameterName->setVisible(true);
		m_focusedParameterValue->setVisible(true);

		if(m_focusedParameterTooltip && dynamic_cast<juce::Slider*>(_component) && _component->isShowing())
		{
			int x = _component->getX();
			int y = _component->getY();

			// local to global
			auto parent = _component->getParentComponent();

			while(parent && parent != this)
			{
				x += parent->getX();
				y += parent->getY();
				parent = parent->getParentComponent();
			}

			x += (_component->getWidth()>>1) - (m_focusedParameterTooltip->getWidth()>>1);
			y += _component->getHeight() + (m_focusedParameterTooltip->getHeight()>>1);

			// global to local of tooltip parent
			parent = m_focusedParameterTooltip->getParentComponent();

			while(parent && parent != this)
			{
				x -= parent->getX();
				y -= parent->getY();
				parent = parent->getParentComponent();
			}

			if(m_focusedParameterTooltip->getProperties().contains("offsetY"))
				y += static_cast<int>(m_focusedParameterTooltip->getProperties()["offsetY"]);

			m_focusedParameterTooltip->setTopLeftPosition(x,y);
			m_focusedParameterTooltip->setText(value, juce::dontSendNotification);
			m_focusedParameterTooltip->setVisible(true);
			m_focusedParameterTooltip->toFront(false);
		}
		else if(m_focusedParameterTooltip)
		{
			m_focusedParameterTooltip->setVisible(false);
		}

		startTimer(3000);
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

		const auto& presets = getController().getSinglePresets();
		const auto& data = presets.front().front().data;
		if(data.empty())
			return;

		std::string m;

		switch(virusLib::Microcontroller::getPresetVersion(data.front()))
		{
		case virusLib::A:	m = "A";	break;
		case virusLib::B:	m = "B";	break;
		case virusLib::C:	m = "C";	break;
		case virusLib::D:	m = "TI";	break;
		case virusLib::D2:	m = "TI2";	break;
		default:			m = "?";	break;
		}

		m_deviceModel->setText(m, juce::dontSendNotification);
		m_deviceModel = nullptr;	// only update once
	}

	void VirusEditor::savePreset()
	{
		juce::PopupMenu menu;

		auto addEntry = [&](juce::PopupMenu& _menu, const std::string& _name, const std::function<void(FileType)>& _callback)
		{
			juce::PopupMenu subMenu;

			subMenu.addItem(".syx", [_callback](){_callback(FileType::Syx); });
			subMenu.addItem(".mid", [_callback](){_callback(FileType::Mid); });

			_menu.addSubMenu(_name, subMenu);
		};

		addEntry(menu, "Current Single (Edit Buffer)", [this](FileType _type)
		{
			savePresets(SaveType::CurrentSingle, _type);
		});

		if(getController().isMultiMode())
		{
			addEntry(menu, "Arrangement (Multi + 16 Singles)", [this](FileType _type)
			{
				savePresets(SaveType::Arrangement, _type);
			});
		}

		juce::PopupMenu banksMenu;
		for(uint8_t b=0; b<static_cast<uint8_t>(getController().getBankCount()); ++b)
		{
			addEntry(banksMenu, getController().getBankName(b), [this, b](const FileType _type)
			{
				savePresets(SaveType::Bank, _type, b);
			});
		}

		menu.addSubMenu("Bank", banksMenu);

		menu.showMenuAsync(juce::PopupMenu::Options());
	}

	void VirusEditor::loadPreset()
	{
		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Choose syx/midi banks to import",
			m_previousPath.isEmpty()
			? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory()
			: m_previousPath,
			"*.syx,*.mid,*.midi", true);

		constexpr auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		const std::function onFileChooser = [this](const juce::FileChooser& chooser)
		{
			if (chooser.getResults().isEmpty())
				return;

			const auto result = chooser.getResult();
			m_previousPath = result.getParentDirectory().getFullPathName();
			const auto ext = result.getFileExtension().toLowerCase();

			std::vector<Patch> patches;
			PatchBrowser::loadBankFile(getController(), patches, nullptr, result);

			if (patches.empty())
				return;

			if (patches.size() == 1)
			{
				// load to edit buffer of current part
				const auto data = getController().modifySingleDump(patches.front().sysex, virusLib::BankNumber::EditBuffer, 
					getController().isMultiMode() ? getController().getCurrentPart() : virusLib::SINGLE, true, true);
				getController().sendSysEx(data);
			}
			else
			{
				// load to bank A
				for(uint8_t i=0; i<static_cast<uint8_t>(patches.size()); ++i)
				{
					const auto data = getController().modifySingleDump(patches[i].sysex, virusLib::BankNumber::A, i, true, false);
					getController().sendSysEx(data);
				}
			}

			getController().onStateLoaded();
		};
		m_fileChooser->launchAsync(flags, onFileChooser);
	}

	void VirusEditor::setPlayMode(uint8_t _playMode)
	{
		const auto playMode = getController().getParameterIndexByName(Virus::g_paramPlayMode);

		getController().getParameter(playMode)->setValue(_playMode, pluginLib::Parameter::ChangedBy::Ui);

		if (_playMode == virusLib::PlayModeSingle && getController().getCurrentPart() != 0)
			m_parameterBinding.setPart(0);

		onPlayModeChanged();
	}

	void VirusEditor::savePresets(SaveType _saveType, FileType _fileType, uint8_t _bankNumber/* = 0*/)
	{
		const auto path = m_processor.getConfig().getValue("virus_bank_dir", "");
		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Save preset(s) as syx or mid",
			m_previousPath.isEmpty()
			? (path.isEmpty() ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() : juce::File(path))
			: m_previousPath,
			"*.syx,*.mid", true);

		constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		auto onFileChooser = [this, _saveType, _bankNumber](const juce::FileChooser& chooser)
		{
			if (chooser.getResults().isEmpty())
				return;

			const auto result = chooser.getResult();
			m_previousPath = result.getParentDirectory().getFullPathName();
			const auto ext = result.getFileExtension().toLowerCase();

			if (!result.existsAsFile() || juce::NativeMessageBox::showYesNoBox(juce::AlertWindow::WarningIcon, "File exists", "Do you want to overwrite the existing file?") == 1)
			{
				savePresets(result.getFullPathName().toStdString(), _saveType, ext.endsWith("mid") ? FileType::Mid : FileType::Syx, _bankNumber);
			}
		};
		m_fileChooser->launchAsync(flags, onFileChooser);
	}

	bool VirusEditor::savePresets(const std::string& _pathName, SaveType _saveType, FileType _fileType, uint8_t _bankNumber/* = 0*/) const
	{
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
				messages.push_back(getController().getMultiEditBuffer().data);

				for(uint8_t i=0; i<16; ++i)
				{
					const auto dump = getController().createSingleDump(i, toMidiByte(virusLib::BankNumber::EditBuffer), i);
					messages.push_back(dump);
				}
			}
			break;
		default:
			return false;
		}

		if(messages.empty())
			return false;

		if(_fileType == FileType::Mid)
		{
			return synthLib::SysexToMidi::write(_pathName.c_str(), messages);
		}

		FILE* hFile = fopen(_pathName.c_str(), "wb");

		if(!hFile)
			return false;

		for (const auto& message : messages)
		{
			const auto written = fwrite(&message[0], 1, message.size(), hFile);

			if(written != message.size())
			{
				fclose(hFile);
				return false;
			}
		}
		fclose(hFile);
		return true;
	}

	void VirusEditor::setPart(size_t _part)
	{
		m_parameterBinding.setPart(static_cast<uint8_t>(_part));
		onCurrentPartChanged();
	}
}
