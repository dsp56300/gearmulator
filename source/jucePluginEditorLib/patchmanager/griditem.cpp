#include "griditem.h"

#include "grid.h"

namespace jucePluginEditorLib::patchManager
{
	GridItem::GridItem(Grid& _grid) : m_grid(_grid)
	{
		setInterceptsMouseClicks(false, true);
		setSize(static_cast<int>(_grid.getItemWidth()), static_cast<int>(_grid.getItemHeight()));
	}

	GridItem::~GridItem()
	{
		delete m_item;
		m_item = nullptr;
	}

	void GridItem::paint(juce::Graphics& _g)
	{
		if(m_index >= static_cast<uint32_t>(m_grid.getNumRows()))
			return;

		m_grid.paintListBoxItem(static_cast<int>(m_index), _g, getWidth(), getHeight(), m_grid.isSelected(m_index));

		juce::Component::paint(_g);
	}

	void GridItem::setItem(const uint32_t _index, juce::Component* _component)
	{
		m_index = _index;
		m_item = _component;

		if(m_item->getParentComponent() != this)
			addAndMakeVisible(m_item);

		m_item->setSize(getWidth(), getHeight());
	}
}
