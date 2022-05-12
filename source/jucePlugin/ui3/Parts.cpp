#include "Parts.h"

#include "VirusEditor.h"

#include "../VirusController.h"
#include "../VirusParameterBinding.h"
#include "../ParameterNames.h"

#include "dsp56kEmu/logging.h"

namespace genericVirusUI
{
	Parts::Parts(VirusEditor& _editor) : m_editor(_editor)
	{
		_editor.findComponents<juce::Button>(m_partSelect, "SelectPart");
		_editor.findComponents<juce::Button>(m_presetPrev, "PresetPrev");
		_editor.findComponents<juce::Button>(m_presetNext, "PresetNext");

		_editor.findComponents<juce::Slider>(m_partVolume, "PartVolume");
		_editor.findComponents<juce::Slider>(m_partPan, "PartPan");
		_editor.findComponents<juce::TextButton>(m_presetName, "PresetName");

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			m_partSelect[i]->onClick = [this, i]{ selectPart(i); };

			if(i < m_presetPrev.size())
				m_presetPrev[i]->onClick = [this, i]{ selectPrevPreset(i); };

			if(i < m_presetNext.size())
				m_presetNext[i]->onClick = [this, i]{ selectNextPreset(i); };

			m_presetName[i]->onClick = [this, i]{ selectPreset(i); };

			const auto partVolume = _editor.getController().getParameterTypeByName(Virus::g_paramPartVolume);
			const auto partPanorama = _editor.getController().getParameterTypeByName(Virus::g_paramPartPanorama);

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

	void Parts::selectPrevPreset(size_t _part) const
	{
		if(m_presetPrev.size() == 1)
			_part = m_editor.getController().getCurrentPart();

		Virus::Controller& controller = m_editor.getController();

		const auto pt = static_cast<uint8_t>(_part);

		if(controller.getCurrentPartProgram(pt) > 0)
		{
            controller.setCurrentPartPreset(pt, controller.getCurrentPartBank(pt), controller.getCurrentPartProgram(pt) - 1);
		}
	}

	void Parts::selectNextPreset(size_t _part) const
	{
		if(m_presetNext.size() == 1)
			_part = m_editor.getController().getCurrentPart();

		Virus::Controller& controller = m_editor.getController();

		const auto pt = static_cast<uint8_t>(_part);
		if(controller.getCurrentPartProgram(pt) < 127)	// FIXME: magic value
		{
            controller.setCurrentPartPreset(pt, controller.getCurrentPartBank(pt), controller.getCurrentPartProgram(pt) + 1);
		}
	}

	void Parts::selectPreset(size_t _part) const
	{
		juce::PopupMenu selector;

		const auto pt = static_cast<uint8_t>(_part);

        for (uint8_t b = 0; b < m_editor.getController().getBankCount(); ++b)
        {
            const auto bank = virusLib::fromArrayIndex(b);
            auto presetNames = m_editor.getController().getSinglePresetNames(bank);
            juce::PopupMenu p;
            for (uint8_t j = 0; j < presetNames.size(); j++)
            {
                const auto presetName = presetNames[j];
                p.addItem(presetName, [this, bank, j, pt] 
                {
                    m_editor.getController().setCurrentPartPreset(pt, bank, j);
                });
            }
            std::stringstream bankName;
            bankName << "Bank " << static_cast<char>('A' + b);
            selector.addSubMenu(std::string(bankName.str()), p);
		}
		selector.showMenuAsync(juce::PopupMenu::Options());
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
