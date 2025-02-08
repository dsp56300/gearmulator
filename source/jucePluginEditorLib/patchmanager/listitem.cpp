#include "listitem.h"

#include "listmodel.h"
#include "patchmanager.h"
#include "savepatchdesc.h"

#include "synthLib/buildconfig.h"

#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	// Juce is funny:
	// Having mouse clicks enabled prevents the list from getting mouse events, i.e. list entry selection is broken.
	// However, by disabling mouse clicks, we also disable the ability to D&D onto these entries, even though D&D are not clicks...
	// We solve both by overwriting hitTest() and return true as long as a D&D is in progress, false otherwise

	ListItem::ListItem(ListModel& _list, const int _row) : m_list(_list), m_row(_row)
	{
//		setInterceptsMouseClicks(false, false);
	}

	void ListItem::paint(juce::Graphics& g)
	{
		Component::paint(g);

		g.setColour(juce::Colour(0xff00ff00));

		if(m_drag == DragType::Above || m_drag == DragType::Over)
			g.drawRect(0, 0, getWidth(), 3, 3);
		if(m_drag == DragType::Below || m_drag == DragType::Over)
			g.drawRect(0, getHeight()-3, getWidth(), 3, 3);
		if(m_drag == DragType::Over)
		{
			g.drawRect(0, 0, 3, getHeight(), 3);
			g.drawRect(getWidth() - 3, 0, 3, getHeight(), 3);
		}
	}

	void ListItem::itemDragEnter(const SourceDetails& dragSourceDetails)
	{
		DragAndDropTarget::itemDragEnter(dragSourceDetails);
		updateDragTypeFromPosition(dragSourceDetails);
	}

	void ListItem::itemDragExit(const SourceDetails& dragSourceDetails)
	{
		DragAndDropTarget::itemDragExit(dragSourceDetails);
		m_drag = DragType::Off;
		repaint();
	}

	void ListItem::itemDragMove(const SourceDetails& dragSourceDetails)
	{
		updateDragTypeFromPosition(dragSourceDetails);
	}

	bool ListItem::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
	{
		if(m_list.getSourceType() != pluginLib::patchDB::SourceType::LocalStorage)
			return false;

		const auto* list = dynamic_cast<const ListModel*>(dragSourceDetails.sourceComponent.get());

		if(list && list == &m_list && m_list.canReorderPatches())
			return true;

		const auto* savePatchDesc = SavePatchDesc::fromDragSource(dragSourceDetails);

		if(!savePatchDesc)
			return false;
		return true;
	}

	void ListItem::itemDropped(const SourceDetails& dragSourceDetails)
	{
		if(m_drag == DragType::Off)
			return;

		auto& pm = m_list.getPatchManager();

		const auto drag = m_drag;
		m_drag = DragType::Off;

		repaint();

		const auto row = drag == DragType::Above ? m_row : m_row + 1;

		const auto patches = SavePatchDesc::getPatchesFromDragSource(dragSourceDetails);

		if(patches.empty())
			return;

		const auto* savePatchDesc = SavePatchDesc::fromDragSource(dragSourceDetails);
		assert(savePatchDesc);

		if(dynamic_cast<const ListModel*>(dragSourceDetails.sourceComponent.get()))
		{
			if(!patches.empty() && pm.movePatchesTo(row, patches))
				m_list.refreshContent();
		}
		else if(patches.size() == 1 && savePatchDesc->isPartValid())
		{
			const auto& source = m_list.getDataSource();
			if(!source)
				return;

			if(drag == DragType::Over)
			{
				repaint();

				const auto existingPatch = m_list.getPatch(m_row);

				if(existingPatch)
				{
					genericUI::MessageBox::showYesNo(juce::MessageBoxIconType::QuestionIcon,
						"Replace Patch", 
						"Do you want to replace the existing patch '" + existingPatch->name + "' with contents of part " + std::to_string(savePatchDesc->getPart() + 1) + "?", 
						[this, existingPatch, patches](genericUI::MessageBox::Result _result)
						{
							if (_result == genericUI::MessageBox::Result::Yes)
							{
								m_list.getPatchManager().replacePatch(existingPatch, patches.front());
							}
						});
					return;
				}
			}

#if SYNTHLIB_DEMO_MODE
			pm.getEditor().showDemoRestrictionMessageBox();
#else
			const auto part = savePatchDesc->getPart();

			pm.copyPatchesTo(source, patches, row, [this, part](const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
			{
				juce::MessageManager::callAsync([this, part, _patches]
				{
					m_list.getPatchManager().setSelectedPatch(part, _patches.front());
				});
			});
#endif

			repaint();
		}

	}

	void ListItem::mouseDown(const juce::MouseEvent& event)
	{
//		m_list.mouseDown(event);
	}

	bool ListItem::hitTest(int x, int y)
	{
		if (const juce::DragAndDropContainer* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
		{
			if (container->isDragAndDropActive())
				return true;
		}

		return false;
	}

	void ListItem::updateDragTypeFromPosition(const SourceDetails& dragSourceDetails)
	{
		const auto prev = m_drag;

		const auto* list = dynamic_cast<const ListModel*>(dragSourceDetails.sourceComponent.get());

		if(list && list == &m_list)
		{
			// list is being sorted
			if (dragSourceDetails.localPosition.y < (getHeight() >> 1))
				m_drag = DragType::Above;
			else
				m_drag = DragType::Below;
		}
		else
		{
			const auto* savePatchDesc = SavePatchDesc::fromDragSource(dragSourceDetails);

			if(savePatchDesc)
			{
				// a patch wants to be saved

				if(m_list.hasFilters())
				{
					// only allow to replace
					m_drag = DragType::Over;
				}
				else
				{
					if (dragSourceDetails.localPosition.y < (getHeight() / 3))
						m_drag = DragType::Above;
					else if (dragSourceDetails.localPosition.y >= (getHeight() * 2 / 3))
						m_drag = DragType::Below;
					else
						m_drag = DragType::Over;
				}
			}
		}

		if (prev != m_drag)
			repaint();
	}
}
