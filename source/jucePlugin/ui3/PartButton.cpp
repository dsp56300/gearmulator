#include "PartButton.h"

#include "VirusEditor.h"
#include "../VirusController.h"
#include "../../jucePluginEditorLib/patchmanager/list.h"

#include "../../jucePluginEditorLib/patchmanager/savepatchdesc.h"

namespace genericVirusUI
{
	PartButton::PartButton(VirusEditor& _editor) : m_editor(_editor)
	{
	}

	void PartButton::initalize(uint8_t _part)
	{
		m_part = _part;
	}

	bool PartButton::isInterestedInDragSource(const SourceDetails& _dragSourceDetails)
	{
		if(m_part > 0 && !m_editor.getController().isMultiMode())
			return false;

		const auto* savePatchDesc = dynamic_cast<const jucePluginEditorLib::patchManager::SavePatchDesc*>(_dragSourceDetails.description.getObject());

		if(savePatchDesc)
			return savePatchDesc->getPart() != m_part;

		const auto patch = getPatchFromDragSource(_dragSourceDetails);
		return patch.first != nullptr;
	}

	void PartButton::itemDropped(const SourceDetails& _dragSourceDetails)
	{
		setIsDragTarget(false);

		const auto* savePatchDesc = dynamic_cast<const jucePluginEditorLib::patchManager::SavePatchDesc*>(_dragSourceDetails.description.getObject());

		auto* pm = m_editor.getPatchManager();

		if(savePatchDesc)
		{
			if(savePatchDesc->getPart() == m_part)
				return;
			pm->copyPart(m_part, static_cast<uint8_t>(savePatchDesc->getPart()));
		}

		const auto patch = getPatchFromDragSource(_dragSourceDetails);
		if(!patch.first)
			return;

		if(pm->getCurrentPart() == m_part)
		{
			patch.second->setSelectedPatches({patch.first});
			patch.second->activateSelectedPatch();
		}
		else
		{
			pm->setSelectedPatch(m_part, patch.first, patch.second->getSearchHandle());
		}
	}

	void PartButton::paint(juce::Graphics& g)
	{
		Button<TextButton>::paint(g);

		if(m_isDragTarget)
		{
			g.setColour(juce::Colour(0xff00ff00));
			g.drawRect(0, 0, getWidth(), getHeight(), 3);
		}
	}

	void PartButton::itemDragEnter(const SourceDetails& _dragSourceDetails)
	{
		DragAndDropTarget::itemDragEnter(_dragSourceDetails);
		if(isInterestedInDragSource(_dragSourceDetails))
			setIsDragTarget(true);
	}

	void PartButton::itemDragExit(const SourceDetails& _dragSourceDetails)
	{
		DragAndDropTarget::itemDragExit(_dragSourceDetails);
		setIsDragTarget(false);
	}

	void PartButton::mouseDrag(const juce::MouseEvent& _event)
	{
		m_editor.startDragging(new jucePluginEditorLib::patchManager::SavePatchDesc(m_part), this);
		m_leftMouseDown = false;
		Button<TextButton>::mouseDrag(_event);
	}

	void PartButton::mouseDown(const juce::MouseEvent& _event)
	{
		if(!_event.mods.isPopupMenu())
			m_leftMouseDown = true;
		Button<TextButton>::mouseDown(_event);
	}

	void PartButton::mouseUp(const juce::MouseEvent& _event)
	{
		if(!_event.mods.isPopupMenu() && m_leftMouseDown)
		{
			m_leftMouseDown = false;
			selectPreset(m_part);
		}
		Button<TextButton>::mouseUp(_event);
	}

	void PartButton::mouseExit(const juce::MouseEvent& event)
	{
		m_leftMouseDown = false;
		Button<TextButton>::mouseExit(event);
	}

	void PartButton::setIsDragTarget(const bool _isDragTarget)
	{
		if(m_isDragTarget == _isDragTarget)
			return;
		m_isDragTarget = _isDragTarget;
		repaint();
	}

	void PartButton::selectPreset(uint8_t _part) const
	{
		juce::PopupMenu selector;

        for (uint8_t b = 0; b < m_editor.getController().getBankCount(); ++b)
        {
            const auto bank = virusLib::fromArrayIndex(b);
            auto presetNames = m_editor.getController().getSinglePresetNames(bank);
            juce::PopupMenu p;
            for (uint8_t j = 0; j < presetNames.size(); j++)
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

	std::pair<pluginLib::patchDB::PatchPtr, jucePluginEditorLib::patchManager::List*> PartButton::getPatchFromDragSource(const SourceDetails& _source)
	{
		auto* list = dynamic_cast<jucePluginEditorLib::patchManager::List*>(_source.sourceComponent.get());
		if(!list)
			return {};

		const auto patches = jucePluginEditorLib::patchManager::List::getPatchesFromDragSource(_source);

		if (patches.size() != 1)
			return {};

		return {patches.front(), list};
	}
}
