#include "list.h"

#include "defaultskin.h"
#include "patchmanager.h"

#include "juceUiLib/uiObject.h"

#include "../pluginEditor.h"

namespace jucePluginEditorLib::patchManager
{
	List::List(PatchManager& _pm) : ListModel(_pm)
	{
		setColour(backgroundColourId, juce::Colour(defaultSkin::colors::background));
		setColour(textColourId, juce::Colour(defaultSkin::colors::itemText));

		setRowHeight(18);

		getViewport()->setScrollBarsShown(true, false);
		setModel(this);
		setMultipleSelectionEnabled(true);

		if (const auto& t = _pm.getTemplate("pm_listbox"))
			t->apply(_pm.getEditor(), *this);

		applyStyleToViewport(_pm, *getViewport());

		setRowSelectedOnMouseDown(false);
	}

	void List::applyStyleToViewport(const PatchManager& _pm, juce::Viewport& _viewport)
	{
		if(const auto t = _pm.getTemplate("pm_scrollbar"))
		{
			t->apply(_pm.getEditor(), _viewport.getVerticalScrollBar());
			t->apply(_pm.getEditor(), _viewport.getHorizontalScrollBar());
		}
		else
		{
			_viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colour(defaultSkin::colors::scrollbar));
			_viewport.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId, juce::Colour(defaultSkin::colors::scrollbar));
			_viewport.getHorizontalScrollBar().setColour(juce::ScrollBar::thumbColourId, juce::Colour(defaultSkin::colors::scrollbar));
			_viewport.getHorizontalScrollBar().setColour(juce::ScrollBar::trackColourId, juce::Colour(defaultSkin::colors::scrollbar));
		}
	}

	juce::Colour List::findColor(const int _colorId)
	{
		return findColour(_colorId);
	}

	const juce::LookAndFeel& List::getStyle() const
	{
		return getLookAndFeel();
	}

	void List::onModelChanged()
	{
		updateContent();
	}

	void List::redraw()
	{
		repaint();
	}

	void List::ensureVisible(const int _row)
	{
		scrollToEnsureRowIsOnscreen(_row);
	}

	int List::getSelectedEntry() const
	{
		return getSelectedRow();
	}

	juce::SparseSet<int> List::getSelectedEntries() const
	{
		return getSelectedRows();
	}

	void List::deselectAll()
	{
		deselectAllRows();
	}

	void List::setSelectedEntries(const juce::SparseSet<int>& _rows)
	{
		setSelectedRows(_rows);
	}

	juce::Rectangle<int> List::getEntryPosition(int _row, const bool _relativeToComponentTopLeft)
	{
		return getRowPosition(_row, _relativeToComponentTopLeft);
	}
}
