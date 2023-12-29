#include "treeitem.h"

#include "list.h"
#include "patchmanager.h"
#include "tree.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"
#include "../../juceUiLib/treeViewStyle.h"

namespace jucePluginEditorLib::patchManager
{
	TreeItem::TreeItem(PatchManager& _patchManager, const std::string& _title, const uint32_t _count/* = g_invalidCount*/) : m_patchManager(_patchManager), m_count(_count)
	{
		setTitle(_title);
	}

	TreeItem::~TreeItem()
	{
		destroyEditorLabel();

		if(m_searchHandle != pluginLib::patchDB::g_invalidSearchHandle)
		{
			getPatchManager().cancelSearch(m_searchHandle);
			m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
		}
	}

	void TreeItem::setTitle(const std::string& _title)
	{
		if (m_title == _title)
			return;
		m_title = _title;
		updateText();
	}

	void TreeItem::setCount(const uint32_t _count)
	{
		if (m_count == _count)
			return;
		m_count = _count;
		updateText();
	}

	void TreeItem::processDirty(const std::set<pluginLib::patchDB::SearchHandle>& _dirtySearches)
	{
		if (_dirtySearches.find(m_searchHandle) == _dirtySearches.end())
			return;

		const auto& search = getPatchManager().getSearch(m_searchHandle);
		if (!search)
			return;

		processSearchUpdated(*search);
	}

	bool TreeItem::hasSearch() const
	{
		return m_searchHandle != pluginLib::patchDB::g_invalidSearchHandle;
	}

	Tree* TreeItem::getTree() const
	{
		return dynamic_cast<Tree*>(getOwnerView());
	}

	void TreeItem::removeFromParent(const bool _destroy) const
	{
		auto* parent = getParentItem();
		if (!parent)
		{
			if (_destroy)
				delete this;
			return;
		}
		const auto idx = getIndexInParent();
		parent->removeSubItem(idx, _destroy);
	}

	void TreeItem::setParent(TreeViewItem* _parent, const bool _sorted/* = false*/)
	{
		const auto* parentExisting = getParentItem();

		if (_parent == parentExisting)
			return;

		removeFromParent(false);

		if (_parent)
		{
			if(_sorted)
				_parent->addSubItemSorted(*this, this);
			else
				_parent->addSubItem(this);
		}
	}

	void TreeItem::itemSelectionChanged(const bool _isNowSelected)
	{
		TreeViewItem::itemSelectionChanged(_isNowSelected);

		if (_isNowSelected)
			getPatchManager().setSelectedSearch(getSearchHandle());
	}

	void TreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		const auto* list = dynamic_cast<List*>(dragSourceDetails.sourceComponent.get());

		if (!list)
			return;

		const auto patches = list->getPatchesFromDragSource(dragSourceDetails);

		if(!patches.empty())
			patchesDropped(patches);
	}

	bool TreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		const auto* list = dynamic_cast<List*>(_dragSourceDetails.sourceComponent.get());

		if (!list)
			return false;

		const auto* arr = _dragSourceDetails.description.getArray();
		if (!arr)
			return false;

		for (const auto& var : *arr)
		{
			if (!var.isInt())
				return false;
		}

		return true;
	}

	void TreeItem::search(pluginLib::patchDB::SearchRequest&& _request)
	{
		setCount(g_unknownCount);

		m_searchHandle = getPatchManager().search(std::move(_request), [](const pluginLib::patchDB::SearchResult&)
		{
		});
	}

	void TreeItem::processSearchUpdated(const pluginLib::patchDB::Search& _search)
	{
		setCount(static_cast<uint32_t>(_search.getResultSize()));
	}

	void TreeItem::setText(const std::string& _text)
	{
		if (m_text == _text)
			return;
		m_text = _text;
		repaintItem();
	}

	void TreeItem::updateText()
	{
		if (m_count == g_invalidCount)
			setText(m_title);
		else if (m_count == g_unknownCount)
			setText(m_title + " (?)");
		else
			setText(m_title + " (" + std::to_string(m_count) + ')');
	}

	void TreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		getTree()->setColour(juce::TreeView::dragAndDropIndicatorColourId, juce::Colour(juce::ModifierKeys::currentModifiers.isShiftDown() ? 0xffff0000 : 0xff00ff00));

		const auto* style = dynamic_cast<const genericUI::TreeViewStyle*>(&getOwnerView()->getLookAndFeel());

		_g.setColour(style ? style->getColor() : juce::Colour(0xffffffff));

		if(style)
		{
			if (const auto f = style->getFont())
				_g.setFont(*f);
		}

		const juce::String t(m_text);
		_g.drawText(t, 0, 0, _width, _height, style ? style->getAlign() : juce::Justification(juce::Justification::centredLeft));
		TreeViewItem::paintItem(_g, _width, _height);
	}

	void TreeItem::editorHidden(juce::Label* _label, juce::TextEditor& _textEditor)
	{
		if (m_editorLabel)
		{
			const auto text = m_editorLabel->getText().toStdString();
			if(text != m_editorInitialText)
				m_finishedEditingCallback(true, text);
			destroyEditorLabel();
		}
		Listener::editorHidden(_label, _textEditor);
	}

	void TreeItem::labelTextChanged(juce::Label* _label)
	{
	}

	int TreeItem::compareElements(const TreeViewItem* _a, const TreeViewItem* _b)
	{
		const auto* a = dynamic_cast<const TreeItem*>(_a);
		const auto* b = dynamic_cast<const TreeItem*>(_b);

		if(a && b)
			return a->getText().compare(b->getText());

		if (_a < _b)
			return -1;
		if (_a > _b)
			return 1;
		return 0;
	}

	bool TreeItem::beginEdit(const std::string& _initialText, FinishedEditingCallback&& _callback)
	{
		if (m_editorLabel)
			return false;

		m_editorInitialText = _initialText;
		m_editorLabel = new juce::Label({}, _initialText);

		const auto pos = getItemPosition(true);

		m_editorLabel->setTopLeftPosition(pos.getTopLeft());
		m_editorLabel->setSize(pos.getWidth(), getItemHeight());

		m_editorLabel->setEditable(true, true, true);
		m_editorLabel->setColour(juce::Label::backgroundColourId, juce::Colour(0xff333333));

		m_editorLabel->addListener(this);

		getOwnerView()->addAndMakeVisible(m_editorLabel);

		m_editorLabel->showEditor();

		m_finishedEditingCallback = std::move(_callback);

		return true;
	}

	void TreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		for (const auto& patch : _patches)
			patchDropped(patch);
	}

	void TreeItem::destroyEditorLabel()
	{
		if (!m_editorLabel)
			return;

		m_editorLabel->getParentComponent()->removeChildComponent(m_editorLabel);
		delete m_editorLabel;
		m_editorLabel = nullptr;

		m_finishedEditingCallback = {};
	}
}
