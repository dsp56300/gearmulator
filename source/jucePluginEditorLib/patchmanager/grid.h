#pragma once

#include "listmodel.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManager
{
	class Grid : public ListModel, public juce::Component
	{
	public:
		Grid(PatchManager& _pm);

		void mouseDown(const juce::MouseEvent& _e) override;
		void mouseUp(const juce::MouseEvent& _e) override;
	private:

		// ListModel
		juce::Colour findColor(int _colorId) override;
		const juce::LookAndFeel& getStyle() const override;
		void onModelChanged() override;
		void redraw() override;
		void ensureVisible(int _row) override;
		int getSelectedEntry() const override;
		juce::SparseSet<int> getSelectedEntries() const override;
		void deselectAll() override;
		void setSelectedEntries(const juce::SparseSet<int>&) override;
		juce::Rectangle<int> getEntryPosition(int _row, bool _relativeToComponentTopLeft) override;

		int m_itemHeight = 22;
		int m_itemWidth = 100;

		juce::Viewport m_viewport;
	};
}
