#include "VirusEditor.h"

#include "BinaryData.h"

#include "../PluginProcessor.h"
#include "../VirusController.h"
#include "../VirusParameterBinding.h"
#include "../version.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(VirusParameterBinding& _binding, Virus::Controller& _controller, AudioPluginAudioProcessor &_processorRef, const std::string& _jsonFilename, std::function<void()> _openMenuCallback) :
		Editor(static_cast<EditorInterface&>(*this)),
		m_processor(_processorRef),
		m_parameterBinding(_binding),
		m_openMenuCallback(std::move(_openMenuCallback))
	{
		create(_jsonFilename);

		m_parts.reset(new Parts(*this));
		m_tabs.reset(new Tabs(*this));
		m_midiPorts.reset(new MidiPorts(*this));
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

		updatePlayModeButtons();

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

		if(auto* deviceModel = findComponentT<juce::Label>("DeviceModel", false))
		{
			std::string m;
			switch(getController().getVirusModel())
			{
			case virusLib::A: m = "A";		break;
			case virusLib::B: m = "B";		break;
			case virusLib::C: m = "C";		break;
			case virusLib::TI: m = "TI";	break;
			}
			deviceModel->setText(m, juce::dontSendNotification);
		}

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
	}

	VirusEditor::~VirusEditor()
	{
		m_parameterBinding.clearBindings();

		getController().onProgramChange = nullptr;
	}

	Virus::Controller& VirusEditor::getController() const
	{
		return m_processor.getController();
	}

	void VirusEditor::setEnabled(juce::Component& _component, bool _enable)
	{
		if(_component.getProperties().contains("disabledAlpha"))
		{
			float a = _component.getProperties()["disabledAlpha"];

			_component.setAlpha(_enable ? 1.0f : a);
			_component.setEnabled(_enable);
		}
		else
		{
			_component.setVisible(_enable);
		}
	}

	const char* VirusEditor::getResourceByFilename(const std::string& _name, uint32_t& _dataSize)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _name)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_dataSize = static_cast<uint32_t>(size);
			return res;
		}

		throw std::runtime_error("Failed to find file named " + _name);
	}

	int VirusEditor::getParameterIndexByName(const std::string& _name)
	{
		return getController().getParameterTypeByName(_name);
	}

	bool VirusEditor::bindParameter(juce::Button& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, static_cast<Virus::ParameterType>(_parameterIndex));
		return true;
	}

	bool VirusEditor::bindParameter(juce::ComboBox& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, static_cast<Virus::ParameterType>(_parameterIndex));
		return true;
	}

	bool VirusEditor::bindParameter(juce::Slider& _target, int _parameterIndex)
	{
		m_parameterBinding.bind(_target, static_cast<Virus::ParameterType>(_parameterIndex));
		return true;
	}

	void VirusEditor::onProgramChange()
	{
		m_parts->onProgramChange();
		updatePresetName();
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

	void VirusEditor::mouseDrag(const juce::MouseEvent & event)
	{
	    updateControlLabel(event.eventComponent);
	}

	void VirusEditor::mouseEnter(const juce::MouseEvent& event)
	{
	    if (event.mouseWasDraggedSinceMouseDown())
	        return;
		updateControlLabel(event.eventComponent);
	}
	void VirusEditor::mouseExit(const juce::MouseEvent& event)
	{
	    if (event.mouseWasDraggedSinceMouseDown())
	        return;
	    updateControlLabel(nullptr);
	}

	void VirusEditor::mouseUp(const juce::MouseEvent& event)
	{
	    if (event.mouseWasDraggedSinceMouseDown())
	        return;
	    updateControlLabel(event.eventComponent);
	}

	void VirusEditor::updateControlLabel(juce::Component* _component) const
	{
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

		const int v = _component->getProperties()["parameter"];

		const auto* p = getController().getParameter(static_cast<Virus::ParameterType>(v));

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

		m_focusedParameterName->setText(desc.name, juce::dontSendNotification);
		m_focusedParameterValue->setText(value, juce::dontSendNotification);

		m_focusedParameterName->setVisible(true);
		m_focusedParameterValue->setVisible(true);

		if(m_focusedParameterTooltip && dynamic_cast<juce::Slider*>(_component))
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

	void VirusEditor::savePreset()
	{
		const auto path = getController().getConfig()->getValue("virus_bank_dir", "");
		m_fileChooser = std::make_unique<juce::FileChooser>(
			"Save preset as syx",
			m_previousPath.isEmpty()
			? (path.isEmpty() ? juce::File::getSpecialLocation(juce::File::currentApplicationFile).getParentDirectory() : juce::File(path))
			: m_previousPath,
			"*.syx", true);

		constexpr auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::FileChooserFlags::canSelectFiles;

		auto onFileChooser = [this](const juce::FileChooser& chooser)
		{
			if (chooser.getResults().isEmpty())
				return;

			const auto result = chooser.getResult();
			m_previousPath = result.getParentDirectory().getFullPathName();
			const auto ext = result.getFileExtension().toLowerCase();
			const uint8_t syxHeader[9] = { 0xF0, 0x00, 0x20, 0x33, 0x01, 0x00, 0x10, 0x01, 0x00 };
			constexpr uint8_t syxEof[1] = { 0xF7 };
			uint8_t cs = syxHeader[5] + syxHeader[6] + syxHeader[7] + syxHeader[8];
			uint8_t data[256];
			for (int i = 0; i < 256; i++)
			{
				const auto param = getController().getParamValue(getController().getCurrentPart(), i < 128 ? 0 : 1, i & 127);

				data[i] = param ? static_cast<int>(param->getValue()) : 0;
				cs += data[i];
			}
			cs = cs & 0x7f;

			result.deleteFile();
			result.create();
			result.appendData(syxHeader, 9);
			result.appendData(data, 256);
			result.appendData(&cs, 1);
			result.appendData(syxEof, 1);
		};
		m_fileChooser->launchAsync(flags, onFileChooser);
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
			PatchBrowser::loadBankFile(patches, nullptr, result);

			if (patches.empty())
				return;

			if (patches.size() == 1)
			{
				// load to edit buffer of current part
				auto data = patches.front().sysex;
				data[7] = virusLib::toMidiByte(virusLib::BankNumber::EditBuffer);
				if (getController().isMultiMode())
					data[8] = getController().getCurrentPart();
				else
					data[8] = virusLib::SINGLE;
				getController().sendSysEx(data);
			}
			else
			{
				// load to bank A
				for (const auto& p : patches)
				{
					auto data = p.sysex;
					data[7] = virusLib::toMidiByte(virusLib::BankNumber::A);
					getController().sendSysEx(data);
				}
			}

			getController().onStateLoaded();
		};
		m_fileChooser->launchAsync(flags, onFileChooser);
	}

	void VirusEditor::setPlayMode(uint8_t _playMode)
	{
	    getController().getParameter(Virus::Param_PlayMode)->setValue(_playMode);
		if (_playMode == virusLib::PlayModeSingle && getController().getCurrentPart() != 0)
			m_parameterBinding.setPart(0);

		onPlayModeChanged();
	}

	void VirusEditor::setPart(size_t _part)
	{
		m_parameterBinding.setPart(static_cast<uint8_t>(_part));
		onCurrentPartChanged();
	}
}
