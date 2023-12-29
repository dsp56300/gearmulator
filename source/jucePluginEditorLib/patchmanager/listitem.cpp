#include "listitem.h"

#include "list.h"
#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	// Juce is funny:
	// Having mouse clicks enabled prevents the list from getting mouse events, i.e. list entry selection is broken.
	// However, by disabling mouse clicks, we also disable the ability to D&D onto these entries, even though D&D are not clicks...
	// We solve both by overwriting hitTest() and return true as long as a D&D is in progress, false otherwise

	ListItem::ListItem(List& _list, const int _row) : m_list(_list), m_row(_row)
	{
//		setInterceptsMouseClicks(false, false);
	}

	void ListItem::paint(juce::Graphics& g)
	{
		Component::paint(g);

		g.setColour(juce::Colour(0xff00ff00));

		if(m_drag == DragType::Above)
			g.drawRect(0, 0, getWidth(), 3, 3);
		else if(m_drag == DragType::Below)
			g.drawRect(0, getHeight()-3, getWidth(), 3, 3);
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
		return true;
	}

	void ListItem::itemDropped(const SourceDetails& dragSourceDetails)
	{
		if(m_drag == DragType::Off)
			return;

		const auto drag = m_drag;
		m_drag = DragType::Off;

		const auto patches = m_list.getPatchesFromDragSource(dragSourceDetails);

		if(!patches.empty() && m_list.getPatchManager().movePatchesTo(drag == DragType::Above ? m_row : m_row + 1, patches))
			m_list.refreshContent();
		else
			repaint();
	}

	void ListItem::mouseDown(const juce::MouseEvent& event)
	{
		m_list.mouseDown(event);
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

		if (dragSourceDetails.localPosition.y < (getHeight() >> 1))
			m_drag = DragType::Above;
		else
			m_drag = DragType::Below;

		if (prev != m_drag)
			repaint();
	}
}
