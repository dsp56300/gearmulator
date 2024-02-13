#include "treeitem.h"

#include "list.h"
#include "patchmanager.h"
#include "savepatchdesc.h"
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
		getPatchManager().removeSelectedItem(getTree(), this);

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

		const auto search = getPatchManager().getSearch(m_searchHandle);
		if (!search)
			return;

		processSearchUpdated(*search);
	}

	bool TreeItem::beginEdit(const std::string& _initialText, FinishedEditingCallback&& _callback)
	{
		auto pos = getItemPosition(true);
		pos.setHeight(getItemHeight());

		return Editable::beginEdit(getOwnerView(), pos, _initialText, std::move(_callback));
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

		if(getTree()->isMultiSelectEnabled())
		{
			if (_isNowSelected)
				getPatchManager().addSelectedItem(getTree(), this);
			else
				getPatchManager().removeSelectedItem(getTree(), this);
		}
		else
		{
			if (_isNowSelected)
				getPatchManager().setSelectedItem(getTree(), this);
		}
	}

	void TreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		if (dynamic_cast<List*>(dragSourceDetails.sourceComponent.get()))
		{
			const auto patches = List::getPatchesFromDragSource(dragSourceDetails);

			if(!patches.empty())
				patchesDropped(patches);
		}
		else
		{
			const auto* desc = SavePatchDesc::fromDragSource(dragSourceDetails);

			if(!desc)
				return;

			if(auto patch = getPatchManager().requestPatchForPart(desc->getPart()))
				patchesDropped({patch});
		}
	}

	bool TreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails)
	{
		const auto* list = dynamic_cast<List*>(_dragSourceDetails.sourceComponent.get());

		if (list)
		{
			const auto* arr = _dragSourceDetails.description.getArray();
			if (!arr)
				return false;

			for (const auto& var : *arr)
			{
				if (!var.isInt())
					return false;
			}

			return isInterestedInPatchList(list, *arr);
		}

		if(const auto* desc = SavePatchDesc::fromDragSource(_dragSourceDetails))
			return isInterestedInSavePatchDesc(*desc);

		return false;
	}

	void TreeItem::search(pluginLib::patchDB::SearchRequest&& _request)
	{
		cancelSearch();
		setCount(g_unknownCount);
		m_searchRequest = _request;
		m_searchHandle = getPatchManager().search(std::move(_request));
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

		const auto color = getColor();

		_g.setColour(color != pluginLib::patchDB::g_invalidColor ? juce::Colour(color) : style ? style->getColor() : juce::Colour(0xffffffff));

		bool haveFont = false;
		if(style)
		{
			if (auto f = style->getFont())
			{
				f->setBold(getParentItem() == getTree()->getRootItem());
				_g.setFont(*f);
				haveFont = true;
			}
		}
		if(!haveFont)
		{
			auto fnt = _g.getCurrentFont();
			fnt.setBold(getParentItem() == getTree()->getRootItem());
			_g.setFont(fnt);
		}

		const juce::String t(m_text);
		_g.drawText(t, 0, 0, _width, _height, style ? style->getAlign() : juce::Justification(juce::Justification::centredLeft));
		TreeViewItem::paintItem(_g, _width, _height);
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

	void TreeItem::setParentSearchRequest(const pluginLib::patchDB::SearchRequest& _parentSearch)
	{
		if(_parentSearch == m_parentSearchRequest)
			return;
		m_parentSearchRequest = _parentSearch;
		onParentSearchChanged(m_parentSearchRequest);
	}

	void TreeItem::cancelSearch()
	{
		if(m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return;

		getPatchManager().cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	void TreeItem::patchesDropped(const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
	{
		for (const auto& patch : _patches)
			patchDropped(patch);
	}
}
