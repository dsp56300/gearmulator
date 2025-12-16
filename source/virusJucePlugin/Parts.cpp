#include "juce_core/juce_core.h"

#include "Parts.h"

#include "PartButton.h"
#include "VirusEditor.h"

#include "VirusController.h"
#include "VirusProcessor.h"
#include "ParameterNames.h"

#include "juceRmlPlugin/rmlPlugin.h"
#include "juceRmlUi/juceRmlComponent.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/ElementDocument.h"

#include "virusLib/device.h"

namespace genericVirusUI
{
	Parts::Parts(VirusEditor& _editor) : m_editor(_editor)
	{
		using namespace juceRmlUi;
		using namespace juceRmlUi::helper;

		auto* doc = _editor.getDocument();
		
		findChildren(m_partSelect, doc, "SelectPart", 16);
		findChildren(m_presetPrev, doc, "PresetPrev");
		findChildren(m_presetNext, doc, "PresetNext");

		findChildren(m_partVolume, doc, "PartVolume");
		findChildren(m_partPan, doc, "PartPan");

		std::vector<Rml::Element*> presetNames;
		findChildren(presetNames, doc, "PresetName");
		findChildren(m_partActive, doc, "PartActive");

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			m_presetName.emplace_back(std::make_unique<PartButton>(presetNames[i], _editor));

			EventListener::Add(m_partSelect[i], Rml::EventId::Click, [this, i](Rml::Event&){ selectPart(i); });
			EventListener::Add(m_partSelect[i], Rml::EventId::Mousedown, [this, i](Rml::Event& _e)
			{
				if (!isContextMenu(_e))
					return;
				selectPartMidiChannel(i);
				_e.StopPropagation();
			});

			if(i < m_presetPrev.size())
				EventListener::AddClick(m_presetPrev[i], [this, i]{ selectPrevPreset(i); });

			if(i < m_presetNext.size())
				EventListener::AddClick(m_presetNext[i], [this, i]{ selectNextPreset(i); });

			m_presetName[i]->initalize(static_cast<uint8_t>(i));

			const auto partVolume = _editor.getController().getParameterIndexByName(virus::g_paramPartVolume);
			const auto partPanorama = _editor.getController().getParameterIndexByName(virus::g_paramPartPanorama);

			auto* binding = _editor.getRmlParameterBinding();

			binding->bind(*m_partVolume[i], virus::g_paramPartVolume, static_cast<uint8_t>(i));
			binding->bind(*m_partPan[i], virus::g_paramPartPanorama, static_cast<uint8_t>(i));

			m_partVolume[i]->SetAttribute("parameter", static_cast<int>(partVolume));
			m_partPan[i]->SetAttribute("parameter", static_cast<int>(partPanorama));

			m_partVolume[i]->SetAttribute("part", static_cast<int>(i));
			m_partPan[i]->SetAttribute("part", static_cast<int>(i));
		}

		updateAll();

		if(!m_partActive.empty())
		{
			for (const auto & partActive : m_partActive)
			{
				partActive->SetProperty(Rml::PropertyId::PointerEvents, Rml::Style::PointerEvents::None);
				setVisible(partActive, false);
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
			m_partSelect[part]->setChecked(true);

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			if(i == part)
				continue;
			m_partSelect[i]->setChecked(false);
		}
	}

	void Parts::updateSingleOrMultiMode() const
	{
		using namespace juceRmlUi::helper;
	    const auto multiMode = m_editor.getController().isMultiMode();

		const auto partCount = multiMode ? static_cast<virus::VirusProcessor&>(m_editor.getProcessor()).getPartCount() : 1;

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			const bool visible = i < partCount;

			jucePluginEditorLib::Editor::setEnabled(m_partSelect[i], visible);
			jucePluginEditorLib::Editor::setEnabled(m_partPan[i], visible);
			jucePluginEditorLib::Editor::setEnabled(m_partVolume[i], visible);

			if(i < m_presetPrev.size())
				jucePluginEditorLib::Editor::setEnabled(m_presetPrev[i], visible);

			if(i < m_presetNext.size())
				jucePluginEditorLib::Editor::setEnabled(m_presetNext[i], visible);

			m_presetName[i]->setVisible(visible);
		}

		const auto volumeParamName = multiMode ? virus::g_paramPartVolume : virus::g_paramPatchVolume;
		const auto volumeParam = m_editor.getController().getParameterIndexByName(volumeParamName);

		m_editor.getRmlParameterBinding()->bind(*m_partVolume[0], volumeParamName, 0);

		m_partVolume[0]->SetAttribute("parameter", static_cast<int>(volumeParam));
	}

	void Parts::timerCallback()
	{
		auto& fpState = m_frontpanelState;

		const uint32_t maxPart = m_editor.getController().isMultiMode() ? 16 : 1;

		bool changed = false;

		for(uint32_t i=0; i<m_partActive.size(); ++i)
		{
			changed |= juceRmlUi::helper::setVisible(m_partActive[i], i < maxPart && fpState.m_midiEventReceived[i]);
			fpState.m_midiEventReceived[i] = false;
		}

		if (changed)
			juceRmlUi::RmlComponent::requestUpdate(m_partActive.front());
	}

	void Parts::updateAll() const
	{
		updatePresetNames();
		updateSelectedPart();
		updateSingleOrMultiMode();
	}
}
