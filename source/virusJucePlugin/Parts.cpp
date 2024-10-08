#include "Parts.h"

#include "PartButton.h"
#include "VirusEditor.h"

#include "VirusController.h"
#include "VirusProcessor.h"
#include "ParameterNames.h"

#include "jucePluginEditorLib/pluginProcessor.h"
#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

#include "jucePluginLib/parameterbinding.h"

#include "virusLib/device.h"

namespace genericVirusUI
{
	Parts::Parts(VirusEditor& _editor) : m_editor(_editor)
	{
		_editor.findComponents<genericUI::Button<juce::DrawableButton>>(m_partSelect, "SelectPart");
		_editor.findComponents<juce::Button>(m_presetPrev, "PresetPrev");
		_editor.findComponents<juce::Button>(m_presetNext, "PresetNext");

		_editor.findComponents<juce::Slider>(m_partVolume, "PartVolume");
		_editor.findComponents<juce::Slider>(m_partPan, "PartPan");
		_editor.findComponents<PartButton>(m_presetName, "PresetName");
		_editor.findComponents<juce::Component>(m_partActive, "PartActive");

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			m_partSelect[i]->onClick = [this, i]{ selectPart(i); };
			m_partSelect[i]->onDown = [this, i](const juce::MouseEvent& _e)
			{
				if(!_e.mods.isPopupMenu())
					return false;
				selectPartMidiChannel(i);
				return true;
			};

			if(i < m_presetPrev.size())
				m_presetPrev[i]->onClick = [this, i]{ selectPrevPreset(i); };

			if(i < m_presetNext.size())
				m_presetNext[i]->onClick = [this, i]{ selectNextPreset(i); };

			m_presetName[i]->initalize(static_cast<uint8_t>(i));

			const auto partVolume = _editor.getController().getParameterIndexByName(virus::g_paramPartVolume);
			const auto partPanorama = _editor.getController().getParameterIndexByName(virus::g_paramPartPanorama);

			_editor.getParameterBinding().bind(*m_partVolume[i], partVolume, static_cast<uint8_t>(i));
			_editor.getParameterBinding().bind(*m_partPan[i], partPanorama, static_cast<uint8_t>(i));

			m_partVolume[i]->getProperties().set("parameter", static_cast<int>(partVolume));
			m_partPan[i]->getProperties().set("parameter", static_cast<int>(partPanorama));

			m_partVolume[i]->getProperties().set("part", static_cast<int>(i));
			m_partPan[i]->getProperties().set("part", static_cast<int>(i));
		}

		updateAll();

		if(!m_partActive.empty())
		{
			for (const auto & partActive : m_partActive)
			{
				partActive->setInterceptsMouseClicks(false, false);
				partActive->setVisible(false);
			}

			startTimer(1000/20);
		}

		m_onFrontpanelStateChanged.set(_editor.getController().onFrontPanelStateChanged, [this](const virusLib::FrontpanelState& _frontpanelState)
		{
			for(size_t i=0; i<m_frontpanelState.m_midiEventReceived.size(); ++i)
				m_frontpanelState.m_midiEventReceived[i] |= _frontpanelState.m_midiEventReceived[i];
		});
	}

	Parts::~Parts() = default;

	void Parts::onProgramChange() const
	{
		updateAll();
	}

	void Parts::onPlayModeChanged() const
	{
		updateAll();
	}

	void Parts::onCurrentPartChanged() const
	{
		updateSelectedPart();
	}

	void Parts::selectPart(const size_t _part) const
	{
		m_editor.setPart(_part);
	}

	void Parts::selectPartMidiChannel(const size_t _part) const
	{
		const auto multiMode = m_editor.getController().isMultiMode();

		if(!multiMode && _part > 0)
			return;

		juce::PopupMenu menu;

		const auto idx = m_editor.getController().getParameterIndexByName(multiMode ? "Part Midi Channel" : "Midi Channel");
		if(idx == pluginLib::Controller::InvalidParameterIndex)
			return;

		const auto v = m_editor.getController().getParameter(idx, static_cast<uint8_t>(multiMode ? _part : 0));

		const std::string name = multiMode ? "Midi Channel " : "Global Midi Channel ";

		for(uint8_t i=0; i<16; ++i)
		{
			menu.addItem(name + ' ' + std::to_string(i + 1), true, v->getUnnormalizedValue() == i, [v, i]
			{
				v->setUnnormalizedValueNotifyingHost(i, pluginLib::Parameter::Origin::Ui);
			});
		}

		menu.showMenuAsync({});
	}

	void Parts::selectPrevPreset(size_t _part) const
	{
		if(m_presetPrev.size() == 1)
			_part = m_editor.getController().getCurrentPart();

		auto* pm = m_editor.getPatchManager();
		if(pm && pm->selectPrevPreset(static_cast<uint32_t>(_part)))
			return;
		m_editor.getController().selectPrevPreset(static_cast<uint8_t>(_part));
	}

	void Parts::selectNextPreset(size_t _part) const
	{
		if(m_presetNext.size() == 1)
			_part = m_editor.getController().getCurrentPart();

		auto* pm = m_editor.getPatchManager();
		if(pm && pm->selectNextPreset(static_cast<uint32_t>(_part)))
			return;
		m_editor.getController().selectNextPreset(static_cast<uint8_t>(_part));
	}

	void Parts::updatePresetNames() const
	{
		for(size_t i=0; i<m_presetName.size(); ++i)
			m_presetName[i]->setButtonText(m_editor.getController().getCurrentPartPresetName(static_cast<uint8_t>(i)));
	}

	void Parts::updateSelectedPart() const
	{
		const auto part = m_editor.getController().getCurrentPart();

		if(part < m_partSelect.size())
			m_partSelect[part]->setToggleState(true, juce::dontSendNotification);

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			if(i == part)
				continue;
			m_partSelect[i]->setToggleState(false, juce::dontSendNotification);
		}
	}

	void Parts::updateSingleOrMultiMode() const
	{
	    const auto multiMode = m_editor.getController().isMultiMode();

		const auto partCount = multiMode ? static_cast<virus::VirusProcessor&>(m_editor.getProcessor()).getPartCount() : 1;

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			const bool visible = i < partCount;

			VirusEditor::setEnabled(*m_partSelect[i], visible);
			VirusEditor::setEnabled(*m_partPan[i], visible);
			VirusEditor::setEnabled(*m_partVolume[i], visible);

			if(i < m_presetPrev.size())
				VirusEditor::setEnabled(*m_presetPrev[i], visible);

			if(i < m_presetNext.size())
				VirusEditor::setEnabled(*m_presetNext[i], visible);

			m_presetName[i]->setVisible(visible);
		}

		const auto volumeParam = m_editor.getController().getParameterIndexByName(multiMode ? virus::g_paramPartVolume : virus::g_paramPatchVolume);
		m_editor.getParameterBinding().bind(*m_partVolume[0], volumeParam, 0);
		m_partVolume[0]->getProperties().set("parameter", static_cast<int>(volumeParam));
	}

	void Parts::timerCallback()
	{
		auto& fpState = m_frontpanelState;

		const uint32_t maxPart = m_editor.getController().isMultiMode() ? 16 : 1;

		for(uint32_t i=0; i<m_partActive.size(); ++i)
		{
			m_partActive[i]->setVisible(i < maxPart && fpState.m_midiEventReceived[i]);
			fpState.m_midiEventReceived[i] = false;
		}
	}

	void Parts::updateAll() const
	{
		updatePresetNames();
		updateSelectedPart();
		updateSingleOrMultiMode();
	}
}
