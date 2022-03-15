#include "VirusEditor.h"

#include "BinaryData.h"
#include "../PluginProcessor.h"
#include "../VirusController.h"
#include "../VirusParameterBinding.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(VirusParameterBinding& _binding, Virus::Controller& _controller, AudioPluginAudioProcessor &_processorRef) : Editor(std::string(BinaryData::VirusC_json, BinaryData::VirusC_jsonSize), _binding, _controller),
		m_parts(*this),
		m_tabs(*this),
		m_patchBrowser(*this)
	{
		m_presetName = findComponentT<juce::Label>("PatchName");
		m_controlLabel = findComponentT<juce::Label>("ControlLabel");
		m_romSelector = findComponentT<juce::ComboBox>("RomSelector");

		m_playModeSingle = findComponentT<juce::Button>("PlayModeSingle");
		m_playModeMulti = findComponentT<juce::Button>("PlayModeMulti");

		m_playModeSingle->onClick = [this]{ setPlayMode(virusLib::PlayMode::PlayModeSingle); };
		m_playModeMulti->onClick = [this]{ setPlayMode(virusLib::PlayMode::PlayModeMulti); };

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

		m_controlLabel->setText("", juce::dontSendNotification);
	}

	void VirusEditor::onProgramChange()
	{
		m_parts.onProgramChange();
		updatePresetName();
	}

	void VirusEditor::onPlayModeChanged()
	{
		m_parts.onPlayModeChanged();
		updatePresetName();
	}

	void VirusEditor::onCurrentPartChanged()
	{
		m_parts.onCurrentPartChanged();
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
			m_controlLabel->setText("", juce::dontSendNotification);
			return;
		}

		const int v = _component->getProperties()["parameter"];

		const auto* p = getController().getParameter(static_cast<Virus::ParameterType>(v));

		if(!p)
		{
			m_controlLabel->setText("", juce::dontSendNotification);
			return;
		}

		const auto value = p->getText(p->getValue(), 0);

		const auto& desc = p->getDescription();

		m_controlLabel->setText(desc.name + "\n" + value, juce::dontSendNotification);
	}

	void VirusEditor::updatePresetName() const
	{
		m_presetName->setText(getController().getCurrentPartPresetName(getController().getCurrentPart()), juce::dontSendNotification);
	}

	void VirusEditor::setPlayMode(uint8_t _playMode)
	{
	    getController().getParameter(Virus::Param_PlayMode)->setValue(_playMode);
		if (_playMode == virusLib::PlayModeSingle && getController().getCurrentPart() != 0)
			getParameterBinding().setPart(0);

		onPlayModeChanged();
	}

	void VirusEditor::setPart(size_t _part)
	{
		getParameterBinding().setPart(static_cast<uint8_t>(_part));
		onCurrentPartChanged();
	}
}
