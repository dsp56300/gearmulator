#include "xtEditor.h"

#include "BinaryData.h"
#include "PluginProcessor.h"
#include "xtArp.h"

#include "xtController.h"
#include "xtFocusedParameter.h"
#include "xtFrontPanel.h"
#include "xtPartName.h"
#include "xtPatchManager.h"
#include "xtWaveEditor.h"

#include "jucePluginEditorLib/midiPorts.h"

#include "jucePluginLib/parameterbinding.h"

namespace xtJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename)
	: jucePluginEditorLib::Editor(_processor, _binding, std::move(_skinFolder))
	, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	, m_parameterBinding(_binding)
	, m_playModeChangeListener(m_controller.onPlayModeChanged)
	{
		create(_jsonFilename);

		m_focusedParameter.reset(new FocusedParameter(m_controller, _binding, *this));

		m_frontPanel.reset(new FrontPanel(*this, m_controller));

		m_parts.reset(new Parts(*this));

		addMouseListener(this, true);

		{
			// Init Patch Manager
			const auto container = findComponent("ContainerPatchManager");
			constexpr auto scale = 1.3f;
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

		m_btMultiMode = findComponentT<juce::Button>("MultiModeButton");
		m_ledMultiMode = findComponentT<juce::Button>("MultiModeLED");

		m_btMultiMode->onClick = [this]
		{
			m_controller.setPlayMode(m_btMultiMode->getToggleState());
		};

		auto updateMultiButton = [this](const bool _isMultiMode)
		{
			m_btMultiMode->setToggleState(_isMultiMode, juce::dontSendNotification);
			m_ledMultiMode->setToggleState(_isMultiMode, juce::dontSendNotification);
		};

		updateMultiButton(m_controller.isMultiMode());

		m_playModeChangeListener = [this, updateMultiButton](const bool& _isMultiMode)
		{
			updateMultiButton(_isMultiMode);
			m_frontPanel->getLcd()->refresh();

			if(!_isMultiMode)
				m_parts->selectPart(0);
		};

		m_btSave = findComponentT<juce::DrawableButton>("SaveButton");
		m_btSave->onClick = [this]
		{
			juce::PopupMenu menu;

			const auto countAdded = getPatchManager()->createSaveMenuEntries(menu);

			if(countAdded)
				menu.showMenuAsync(juce::PopupMenu::Options());
		};

		if(auto* btWavePlus = findComponentT<juce::Button>("wtPlus", false))
		{
			btWavePlus->onClick = [this]
			{
				changeWave(1);
			};
		}

		if(auto* btWaveMinus = findComponentT<juce::Button>("wtMinus", false))
		{
			btWaveMinus->onClick = [this]
			{
				changeWave(-1);
			};
		}

		auto* btPatchPrev = findComponentT<juce::Button>("patchPrev");
		btPatchPrev->onClick = [this]
		{
			getPatchManager()->selectPrevPreset(m_controller.getCurrentPart());
		};

		auto* btPatchNext = findComponentT<juce::Button>("patchNext");
		btPatchNext->onClick = [this]
		{
			getPatchManager()->selectNextPreset(m_controller.getCurrentPart());
		};

#if defined(_DEBUG) && defined(_WIN32)
		assert(m_waveEditor);
		m_waveEditor->initialize();
#else
		auto* waveEditorButtonParent = findComponent("waveEditorButtonParent");
		waveEditorButtonParent->setVisible(false);
#endif

		m_midiPorts.reset(new jucePluginEditorLib::MidiPorts(*this, getProcessor()));

		m_arp.reset(new Arp(*this));
	}

	Editor::~Editor()
	{
		m_arp.reset();
		if(m_waveEditor)
			m_waveEditor->destroy();
		getXtController().setWaveEditor(nullptr);
		m_frontPanel.reset();
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {"Vavra",
			"Xenia runs in demo mode\n"
			"\n"
			"The following features are disabled:\n"
			"- Saving/Exporting Presets\n"
			"- Plugin state is not preserve"
		};
	}

	XtLcd* Editor::getLcd() const
	{
		return m_frontPanel->getLcd();
	}

	Parts& Editor::getParts() const
	{
		assert(m_parts);
		return *m_parts;
	}

	genericUI::Button<juce::DrawableButton>* Editor::createJuceComponent(
		genericUI::Button<juce::DrawableButton>* _button, genericUI::UiObject& _object, const std::string& _name,
		const juce::DrawableButton::ButtonStyle _buttonStyle)
	{
		if(_name == "PartButtonSmall")
			return new PartButton(*this, _name, _buttonStyle);

		return jucePluginEditorLib::Editor::createJuceComponent(_button, _object, _name, _buttonStyle);
	}

	genericUI::Button<juce::TextButton>* Editor::createJuceComponent(genericUI::Button<juce::TextButton>* _button,
		genericUI::UiObject& _object)
	{
		if(_object.getName() == "PatchName")
			return new PartName(*this);

		return jucePluginEditorLib::Editor::createJuceComponent(_button, _object);
	}

	juce::Component* Editor::createJuceComponent(juce::Component* _component, genericUI::UiObject& _object)
	{
		if(_object.getName() == "waveEditorContainer")
		{
			const auto configOptions = getProcessor().getConfigOptions();
			const auto dir = configOptions.getDefaultFile().getParentDirectory();

			m_waveEditor = new WaveEditor(*this, dir);
			getXtController().setWaveEditor(m_waveEditor);
			return m_waveEditor;
		}
		return jucePluginEditorLib::Editor::createJuceComponent(_component, _object);
	}

	void Editor::setCurrentPart(const uint8_t _part)
	{
		m_controller.setCurrentPart(_part);
		m_parameterBinding.setPart(_part);

		jucePluginEditorLib::Editor::setCurrentPart(_part);

		m_frontPanel->getLcd()->refresh();
	}

	void Editor::mouseEnter(const juce::MouseEvent& _event)
	{
		m_focusedParameter->onMouseEnter(_event);
	}

	void Editor::changeWave(const int _step) const
	{
		if(!_step)
			return;

		uint32_t index;
		if(!m_controller.getParameterDescriptions().getIndexByName(index, "Wave"))
			return;

		auto* p = m_controller.getParameter(index, m_controller.getCurrentPart());

		if(!p)
			return;

		const auto& desc = p->getDescription();
		const auto& range = desc.range;

		int v = p->getUnnormalizedValue();

		const auto& valList = desc.valueList;

		while(v >= range.getStart() && v <= range.getEnd())
		{
			v += _step;
			const auto newText = valList.valueToText(v);

			if(newText.empty())
				continue;

			p->setUnnormalizedValueNotifyingHost(v, pluginLib::Parameter::Origin::Ui);
			break;
		}
	}
}
