#include "PartButton.h"

#include "VirusEditor.h"
#include "../VirusController.h"
#include "../../jucePluginEditorLib/patchmanager/list.h"

#include "../../jucePluginEditorLib/patchmanager/savepatchdesc.h"

namespace genericVirusUI
{
	PartButton::PartButton(VirusEditor& _editor) : jucePluginEditorLib::PartButton<TextButton>(_editor), m_editor(_editor)  // NOLINT(clang-diagnostic-undefined-func-template)
	{
	}

	bool PartButton::isInterestedInDragSource(const SourceDetails& _dragSourceDetails)
	{
		if(getPart() > 0 && !m_editor.getController().isMultiMode())
			return false;

		return jucePluginEditorLib::PartButton<TextButton>::isInterestedInDragSource(_dragSourceDetails);  // NOLINT(clang-diagnostic-undefined-func-template)
	}

	void PartButton::paint(juce::Graphics& g)
	{
		jucePluginEditorLib::PartButton<TextButton>::paint(g);
	}

	void PartButton::onClick()
	{
		selectPreset(getPart());
	}

	void PartButton::selectPreset(uint8_t _part) const
	{
		juce::PopupMenu selector;

        for (uint8_t b = 0; b < static_cast<uint8_t>(m_editor.getController().getBankCount()); ++b)
        {
            const auto bank = virusLib::fromArrayIndex(b);
            auto presetNames = m_editor.getController().getSinglePresetNames(bank);
            juce::PopupMenu p;
            for (uint8_t j = 0; j < static_cast<uint8_t>(presetNames.size()); j++)
            {
                const auto& presetName = presetNames[j];
                p.addItem(presetName, [this, bank, j, _part] 
                {
					m_editor.selectRomPreset(_part, bank, j);
                });
            }
            selector.addSubMenu(m_editor.getController().getBankName(b), p);
		}
		selector.showMenuAsync(juce::PopupMenu::Options());
	}
}
