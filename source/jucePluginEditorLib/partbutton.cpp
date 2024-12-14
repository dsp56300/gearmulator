#include "partbutton.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"

#include "patchmanager/patchmanager.h"
#include "patchmanager/savepatchdesc.h"
#include "patchmanager/listmodel.h"

namespace jucePluginEditorLib
{
	namespace
	{
		std::pair<pluginLib::patchDB::PatchPtr, patchManager::ListModel*> getPatchFromDragSource(const juce::DragAndDropTarget::SourceDetails& _source)
		{
			auto* list = dynamic_cast<patchManager::ListModel*>(_source.sourceComponent.get());
			if(!list)
				return {};

			const auto& patches = patchManager::SavePatchDesc::getPatchesFromDragSource(_source);

			if (patches.size() != 1)
				return {};

			return {patches.front(), list};
		}
	}

	template <typename T> bool PartButton<T>::isInterestedInDragSource(const SourceDetails& _dragSourceDetails)
	{
		const auto* savePatchDesc = patchManager::SavePatchDesc::fromDragSource(_dragSourceDetails);

		if(savePatchDesc)
			return !savePatchDesc->isPartValid() || savePatchDesc->getPart() != getPart();

		const auto patch = getPatchFromDragSource(_dragSourceDetails);
		return patch.first != nullptr;
	}

	template <typename T> bool PartButton<T>::isInterestedInFileDrag(const juce::StringArray& _files)
	{
		if(_files.size() != 1)
			return false;
		return true;
	}

	template <typename T> void PartButton<T>::fileDragEnter(const juce::StringArray& files, int x, int y)
	{
		if(isInterestedInFileDrag(files))
			setIsDragTarget(true);
		FileDragAndDropTarget::fileDragEnter(files, x, y);
	}

	template <typename T> void PartButton<T>::fileDragExit(const juce::StringArray& files)
	{
		FileDragAndDropTarget::fileDragExit(files);
		setIsDragTarget(false);
	}

	template <typename T> void PartButton<T>::filesDropped(const juce::StringArray& _files, int, int)
	{
		setIsDragTarget(false);

		auto* pm = m_editor.getPatchManager();
		if(!pm)
			return;

		if(!pm->activatePatch(_files.begin()->toStdString(), getPart()))
		{
			juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, 
				m_editor.getProcessor().getProperties().name, 
				"Failed to load patch. Make sure that the format is supported and that the file only contains one patch.\n"
				"\n"
				"Drag files with multiple patches onto the Patch Manager instead to import them", nullptr, juce::ModalCallbackFunction::create([](int){}));
		}
	}

	template <typename T> void PartButton<T>::itemDragEnter(const SourceDetails& dragSourceDetails)
	{
		if(isInterestedInDragSource(dragSourceDetails))
			setIsDragTarget(true);
	}

	template <typename T> void PartButton<T>::itemDragExit(const SourceDetails& _dragSourceDetails)
	{
		DragAndDropTarget::itemDragExit(_dragSourceDetails);
		setIsDragTarget(false);
	}

	template <typename T> void PartButton<T>::itemDropped(const SourceDetails& _dragSourceDetails)
	{
		setIsDragTarget(false);

		const auto* savePatchDesc = patchManager::SavePatchDesc::fromDragSource(_dragSourceDetails);

		auto* pm = m_editor.getPatchManager();

		if(savePatchDesc)
		{
			// part is not valid if the drag source is the patch manager
			if(savePatchDesc->isPartValid())
			{
				if(savePatchDesc->getPart() == getPart())
					return;
				pm->copyPart(m_part, static_cast<uint8_t>(savePatchDesc->getPart()));
			}
		}

		const auto [patch, list] = getPatchFromDragSource(_dragSourceDetails);
		if(!patch)
			return;

		if(pm->getCurrentPart() == m_part)
		{
			list->setSelectedPatches({patch});
			list->activateSelectedPatch();
		}
		else
		{
			pm->setSelectedPatch(m_part, patch, list->getSearchHandle());
		}
	}

	template <typename T> void PartButton<T>::paint(juce::Graphics& g)
	{
		genericUI::Button<T>::paint(g);

		if(m_isDragTarget)
		{
			g.setColour(juce::Colour(0xff00ff00));
			g.drawRect(0, 0, genericUI::Button<T>::getWidth(), genericUI::Button<T>::getHeight(), 3);
		}
	}
	
	template <typename T> void PartButton<T>::mouseDrag(const juce::MouseEvent& _event)
	{
		auto* patchManager = m_editor.getPatchManager();
		if(patchManager)
			m_editor.startDragging(new patchManager::SavePatchDesc(*patchManager, m_part), this);
		genericUI::Button<T>::mouseDrag(_event);
	}

	template <typename T> void PartButton<T>::mouseUp(const juce::MouseEvent& _event)
	{
		if(!_event.mods.isPopupMenu() && genericUI::Button<T>::isDown() && genericUI::Button<T>::isOver())
			onClick();

		genericUI::Button<T>::mouseUp(_event);
	}

	template <typename T> void PartButton<T>::setIsDragTarget(const bool _isDragTarget)
	{
		if(m_isDragTarget == _isDragTarget)
			return;
		m_isDragTarget = _isDragTarget;
		genericUI::Button<T>::repaint();
	}

	template class PartButton<juce::TextButton>;
	template class PartButton<juce::DrawableButton>;
	template class PartButton<juce::ImageButton>;
}
