#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace juce
{
	class Graphics;
}

namespace jucePluginEditorLib::patchManager
{
	class Grid;

	class GridItem : public juce::Component
	{
	public:
		GridItem(Grid& _grid);
		~GridItem();

		void paint(juce::Graphics& _g) override;

		void setItem(uint32_t _index, juce::Component* _component);
		juce::Component* getItem() const { return m_item; }
	private:
		Grid& m_grid;
		uint32_t m_index = ~0;
		juce::Component* m_item = nullptr;
	};
}
