#include "Parts.h"

#include "VirusEditor.h"
#include "../VirusController.h"
#include "../VirusParameterBinding.h"
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
			m_presetPrev[i]->onClick = [this, i]{ selectPrevPreset(i); };
			m_presetNext[i]->onClick = [this, i]{ selectNextPreset(i); };
			m_presetName[i]->onClick = [this, i]{ selectPreset(i); };

			_editor.getParameterBinding().bind(*m_partVolume[i], Virus::Param_PartVolume, static_cast<uint8_t>(i));
			_editor.getParameterBinding().bind(*m_partPan[i], Virus::Param_Panorama, static_cast<uint8_t>(i));
		}

		updateAll();
	}

	Parts::~Parts()
	{
	}

	void Parts::onProgramChange()
	{
		updateAll();
	}

	void Parts::selectPart(const size_t _part)
	{
		m_editor.getController().setCurrentPart(static_cast<uint8_t>(_part));
	}

	void Parts::selectPrevPreset(const size_t _part)
	{
		Virus::Controller& controller = m_editor.getController();

		const auto pt = static_cast<uint8_t>(_part);

		if(controller.getCurrentPartProgram(pt) > 0)
		{
            controller.setCurrentPartPreset(pt, controller.getCurrentPartBank(pt), controller.getCurrentPartProgram(pt) - 1);
		}
	}

	void Parts::selectNextPreset(const size_t _part)
	{
		Virus::Controller& controller = m_editor.getController();

		const auto pt = static_cast<uint8_t>(_part);
		if(controller.getCurrentPartProgram(pt) < 127)	// FIXME: magic value
		{
            controller.setCurrentPartPreset(pt, controller.getCurrentPartBank(pt), controller.getCurrentPartProgram(pt) + 1);
		}
	}

	void Parts::selectPreset(size_t _part)
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
                p.addItem(presetNames[j], [this, bank, j, pt, presetName] 
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

	void Parts::updatePresetNames()
	{
		for(size_t i=0; i<m_presetName.size(); ++i)
			m_presetName[i]->setButtonText(m_editor.getController().getCurrentPartPresetName(static_cast<uint8_t>(i)));
	}

	void Parts::updateSelectedPart()
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

	void Parts::updateSingleOrMultiMode()
	{
	    const auto multiMode = m_editor.getController().isMultiMode();

		for(size_t i=0; i<m_partSelect.size(); ++i)
		{
			const bool visible = multiMode || !i;

			m_partSelect[i]->setVisible(visible);
			m_partPan[i]->setVisible(visible);
			m_partVolume[i]->setVisible(visible);
			m_presetPrev[i]->setVisible(visible);
			m_presetNext[i]->setVisible(visible);
			m_presetName[i]->setVisible(visible);
		}
	}

	void Parts::updateAll()
	{
		updatePresetNames();
		updateSelectedPart();
		updateSingleOrMultiMode();
	}
}
