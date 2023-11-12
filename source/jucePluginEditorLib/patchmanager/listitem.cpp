#include "listitem.h"

#include "list.h"

namespace jucePluginEditorLib::patchManager
{
	// Juce is funny:
	// Having mouse clicks enabled prevents the list from getting mouse events, i.e. list entry selection is broken.
	// However, by disabling mouse clicks, we also disable the ability to D&D onto these entries, even though D&D are not clicks...
	// We solve both by overwriting hitTest() and return true as long as a D&D is in progress, false otherwise

	ListItem::ListItem(List& _list) : m_list(_list)
	{
//		setInterceptsMouseClicks(false, false);
	}

	void ListItem::paint(juce::Graphics& g)
	{
		Component::paint(g);

//		if(m_drag)
//			g.drawRect(0, getHeight()>>1, getWidth(), 3, 3);
	}

	void ListItem::itemDragEnter(const SourceDetails& dragSourceDetails)
	{
		DragAndDropTarget::itemDragEnter(dragSourceDetails);
		m_drag = true;
	}

	void ListItem::itemDragExit(const SourceDetails& dragSourceDetails)
	{
		DragAndDropTarget::itemDragExit(dragSourceDetails);
		m_drag = false;
	}

	bool ListItem::isInterestedInDragSource(const SourceDetails& dragSourceDetails)
	{
		return true;
	}

	void ListItem::itemDropped(const SourceDetails& dragSourceDetails)
	{
		m_drag = false;
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
}
