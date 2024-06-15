#include "weTree.h"

#include "weTreeItem.h"
#include "xtEditor.h"
#include "xtWaveEditor.h"

namespace xtJucePlugin
{
	Tree::Tree(WaveEditor& _editor) : m_editor(_editor)
	{
		setRootItem(new TreeItem());
		setRootItemVisible(false);

		auto& editor = _editor.getEditor();

		if(const auto t = editor.getTemplate("pm_treeview"))
			t->apply(editor, *this);

		getViewport()->setScrollBarsShown(true, true);

		if(const auto t = editor.getTemplate("pm_scrollbar"))
		{
			t->apply(editor, getViewport()->getVerticalScrollBar());
			t->apply(editor, getViewport()->getHorizontalScrollBar());
		}
	}

	Tree::~Tree()
	{
		deleteRootItem();
	}

	void Tree::parentHierarchyChanged()
	{
		TreeView::parentHierarchyChanged();

		const auto* parent = getParentComponent();
		if(!parent)
			return;

		const auto w = static_cast<float>(parent->getWidth()) / g_waveEditorScale;
		const auto h = static_cast<float>(parent->getHeight()) / g_waveEditorScale;

		setTransform(juce::AffineTransform::scale(g_waveEditorScale));
		setSize(static_cast<int>(w), static_cast<int>(h));
	}

	void Tree::paint(juce::Graphics& _g)
	{
		juce::TreeView::paint(_g);
	}
}
