#include "treeitem.h"

#include "list.h"
#include "patchmanager.h"

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

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

		const auto* arr = dragSourceDetails.description.getArray();
		if (!arr)
			return;

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& var : *arr)
		{
			if (!var.isInt())
				continue;
			const int idx = var;
			const auto patch = list->getPatch(idx);
			if (patch)
				patches.push_back(patch);
		}

		if(!patches.empty())
			patchesDropped(patches);
	}

	bool TreeItem::isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& dragSourceDetails)
	{
		const auto* list = dynamic_cast<List*>(dragSourceDetails.sourceComponent.get());

		if (!list)
			return false;

		const auto* arr = dragSourceDetails.description.getArray();
		if (!arr)
			return false;

		for (auto var : *arr)
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
		setCount(static_cast<uint32_t>(_search.results.size()));
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
		_g.setColour(juce::Colour(0xffffffff));
		const juce::String t(m_text);
		_g.drawText(t, 0, 0, _width, _height, juce::Justification(juce::Justification::centredLeft));
		TreeViewItem::paintItem(_g, _width, _height);
	}

	void TreeItem::editorHidden(juce::Label* _label, juce::TextEditor& _textEditor)
	{
		if (m_editorLabel)
		{
			const auto text = m_editorLabel->getText();
			m_finishedEditingCallback(true, text.toStdString());
			destroyEditorLabel();
		}
		Listener::editorHidden(_label, _textEditor);
	}

	void TreeItem::labelTextChanged(juce::Label* _label)
	{
	}

	bool TreeItem::beginEdit(const std::string& _initialText, FinishedEditingCallback&& _callback)
	{
		if (m_editorLabel)
			return false;

		m_editorLabel = new juce::Label({}, _initialText);

		const auto pos = getItemPosition(true);

		m_editorLabel->setTopLeftPosition(pos.getTopLeft());
		m_editorLabel->setSize(pos.getWidth(), pos.getHeight());

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
		{
			patchDropped(patch);
		}
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
