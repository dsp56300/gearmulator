#include "Parts.h"

#include "PartButton.h"
#include "VirusEditor.h"

#include "../VirusController.h"
#include "../ParameterNames.h"

#include "../../jucePluginLib/parameterbinding.h"

#include "../../jucePluginEditorLib/patchmanager/savepatchdesc.h"

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

			const auto partVolume = _editor.getController().getParameterIndexByName(Virus::g_paramPartVolume);
			const auto partPanorama = _editor.getController().getParameterIndexByName(Virus::g_paramPartPanorama);

			_editor.getParameterBinding().bind(*m_partVolume[i], partVolume, static_cast<uint8_t>(i));
			_editor.getParameterBinding().bind(*m_partPan[i], partPanorama, static_cast<uint8_t>(i));

			m_partVolume[i]->getProperties().set("parameter", static_cast<int>(partVolume));
			m_partPan[i]->getProperties().set("parameter", static_cast<int>(partPanorama));

			m_partVolume[i]->getProperties().set("part", static_cast<int>(i));
			m_partPan[i]->getProperties().set("part", static_cast<int>(i));
		}

		updateAll();
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
		if(!m_editor.getController().isMultiMode())
			return;

		juce::PopupMenu menu;

		const auto idx= m_editor.getController().getParameterIndexByName("Part Midi Channel");
		if(idx == pluginLib::Controller::InvalidParameterIndex)
			return;

		const auto v = m_editor.getController().getParameter(idx, static_cast<uint8_t>(_part));

		for(uint8_t i=0; i<16; ++i)
		{
			menu.addItem("Midi Channel " + std::to_string(i + 1), true, v->getUnnormalizedValue() == i, [v, i]
			{
				v->setValue(v->convertTo0to1(i), pluginLib::Parameter::ChangedBy::Ui);
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

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			const bool visible = multiMode || !i;

			VirusEditor::setEnabled(*m_partSelect[i], visible);
			VirusEditor::setEnabled(*m_partPan[i], visible);
			VirusEditor::setEnabled(*m_partVolume[i], visible);

			if(i < m_presetPrev.size())
				VirusEditor::setEnabled(*m_presetPrev[i], visible);

			if(i < m_presetNext.size())
				VirusEditor::setEnabled(*m_presetNext[i], visible);

			m_presetName[i]->setVisible(visible);
		}
	}

	void Parts::updateAll() const
	{
		updatePresetNames();
		updateSelectedPart();
		updateSingleOrMultiMode();
	}
}
