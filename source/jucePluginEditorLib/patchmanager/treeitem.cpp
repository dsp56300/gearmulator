#include "treeitem.h"

#include "list.h"
#include "patchmanager.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace jucePluginEditorLib::patchManager
{
	TreeItem::TreeItem(PatchManager& _patchManager, std::string _title) : m_patchManager(_patchManager), m_title(std::move(_title))
	{
	}

	void TreeItem::setTitle(const std::string& _title)
	{
		if (m_title == _title)
			return;
		m_title = _title;
		repaintItem();
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

	void TreeItem::itemSelectionChanged(const bool _isNowSelected)
	{
		TreeViewItem::itemSelectionChanged(_isNowSelected);

		if (_isNowSelected)
			getPatchManager().setSelectedSearch(getSearchHandle());
	}

	void TreeItem::itemDropped(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails, int insertIndex)
	{
		TreeViewItem::itemDropped(dragSourceDetails, insertIndex);
	}

	bool TreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		const auto* list = dynamic_cast<List*>(dragSourceDetails.sourceComponent.get());

		if (!list)
			return false;

		if (m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return false;

		return true;
	}

	void TreeItem::search(pluginLib::patchDB::SearchRequest&& _request)
	{
		m_searchHandle = getPatchManager().search(std::move(_request), [](const pluginLib::patchDB::SearchResult&)
		{
		});
	}

	void TreeItem::paintItem(juce::Graphics& _g, const int _width, const int _height)
	{
		_g.setColour(juce::Colour(0xffffffff));
		_g.drawText(m_title.c_str(), 0, 0, _width, _height, juce::Justification(juce::Justification::centredLeft));
		TreeViewItem::paintItem(_g, _width, _height);
	}
}
