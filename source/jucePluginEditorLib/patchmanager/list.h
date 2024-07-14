#pragma once

#include "listmodel.h"

namespace jucePluginEditorLib::patchManager
{
	class List : public ListModel, public juce::ListBox
	{
	public:
		explicit List(PatchManager& _pm);

		static void applyStyleToViewport(const PatchManager& _pm, juce::Viewport& _viewport);

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
	};
}
