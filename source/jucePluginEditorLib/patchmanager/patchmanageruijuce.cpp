#include "patchmanageruijuce.h"

#include "datasourcetree.h"
#include "datasourcetreeitem.h"
#include "grid.h"
#include "list.h"
#include "patchmanager.h"
#include "tagstree.h"
#include "tree.h"
#include "treeitem.h"

#include "jucePluginEditorLib/pluginEditor.h"
#include "jucePluginEditorLib/pluginProcessor.h"

#if JUCE_MAJOR_VERSION < 8	// they forgot this include but fixed it in version 8+
#include "juce_gui_extra/misc/juce_ColourSelector.h"
#endif

namespace jucePluginEditorLib::patchManager
{
	constexpr int g_scale = 2;
	constexpr auto g_searchBarHeight = 32;
	constexpr int g_padding = 4;

	PatchManagerUiJuce::PatchManagerUiJuce(Editor& _editor, PatchManager& _db, Component* _root, const std::initializer_list<GroupType>& _groupTypes) : PatchManagerUi(_editor, _db)
	{
		const auto rootW = _root->getWidth() / g_scale;
		const auto rootH = _root->getHeight() / g_scale;
		const auto scale = juce::AffineTransform::scale(g_scale);

		setSize(rootW, rootH);
		setTransform(scale);

		_root->addAndMakeVisible(this);

		auto weight = [&](int _weight)
		{
			return rootW * _weight / 100;
		};

		// 1st column
		auto w = weight(33);
		m_treeDS = new DatasourceTree(*this, _groupTypes);
		m_treeDS->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		addAndMakeVisible(m_treeDS);

		// 2nd column
		w = weight(20);
		m_treeTags = new TagsTree(*this);
		m_treeTags->setTopLeftPosition(m_treeDS->getRight() + g_padding, 0);
		m_treeTags->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		addAndMakeVisible(m_treeTags);

		// 3rd column
		w = weight(15);
		m_list = new List(*this);
		m_list->setTopLeftPosition(m_treeTags->getRight() + g_padding, 0);
		m_list->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		m_grid = new Grid(*this);
		m_grid->setTopLeftPosition(m_list->getPosition());
		m_grid->setSize(m_list->getWidth(), m_list->getHeight());

		m_grid->setLookAndFeel(&m_list->getLookAndFeel());
		m_grid->setColour(juce::ListBox::backgroundColourId, m_list->findColour(juce::ListBox::backgroundColourId));
		m_grid->setColour(juce::ListBox::textColourId, m_list->findColour(juce::ListBox::textColourId));
		m_grid->setColour(juce::ListBox::outlineColourId, m_list->findColour(juce::ListBox::outlineColourId));

		addAndMakeVisible(m_list);
		addChildComponent(m_grid);

		juce::StretchableLayoutManager lm;

		m_stretchableManager.setItemLayout(0, 100, rootW * 0.5, m_treeDS->getWidth());		m_stretchableManager.setItemLayout(1, 5, 5, 5);
		m_stretchableManager.setItemLayout(2, 100, rootW * 0.5, m_treeTags->getWidth());	m_stretchableManager.setItemLayout(3, 5, 5, 5);
		m_stretchableManager.setItemLayout(4, 100, rootW * 0.5, m_list->getWidth());		m_stretchableManager.setItemLayout(5, 5, 5, 5);

		m_resizerBarA.setSize(5, rootH);
		m_resizerBarB.setSize(5, rootH);
		m_resizerBarC.setSize(5, rootH);

		addAndMakeVisible(m_resizerBarA);
		addAndMakeVisible(m_resizerBarB);
		addAndMakeVisible(m_resizerBarC);

		PatchManagerUiJuce::resized();

		const auto& config = getEditor().getProcessor().getConfig();

		if(config.getIntValue("pm_layout", static_cast<int>(LayoutType::List)) == static_cast<int>(LayoutType::Grid))
			setLayout(LayoutType::Grid);
		else
			PatchManagerUiJuce::resized();
	}

	PatchManagerUiJuce::~PatchManagerUiJuce()
	{
		delete m_list;
		delete m_grid;

		// trees emit onSelectionChanged, be sure to guard it 
		m_list = nullptr;
		m_grid = nullptr;

		delete m_treeTags;
		delete m_treeDS;
	}

	void PatchManagerUiJuce::setListStatus(const uint32_t _selected, const uint32_t _total) const
	{
//		m_status->setListStatus(_selected, _total);
	}

	ListModel* PatchManagerUiJuce::getListModel() const
	{
		if(m_layout == LayoutType::List)
			return m_list;
		return m_grid;
	}

	void PatchManagerUiJuce::setLayout(const LayoutType _layout)
	{
		if(m_layout == _layout)
			return;

		auto* oldModel = getListModel();

		m_layout = _layout;

		auto* newModel = getListModel();

		newModel->setContent(oldModel->getSearchHandle());
		newModel->setSelectedEntries(oldModel->getSelectedEntries());

		dynamic_cast<Component*>(newModel)->setVisible(true);
		dynamic_cast<Component*>(oldModel)->setVisible(false);

		if(m_firstTimeGridLayout && _layout == LayoutType::Grid)
		{
			m_firstTimeGridLayout = false;
			setGridLayout128();
		}
		else
		{
			resized();
		}

		auto& config = getEditor().getProcessor().getConfig();
		config.setValue("pm_layout", static_cast<int>(_layout));
		config.saveIfNeeded();
	}

	bool PatchManagerUiJuce::setGridLayout128()
	{
		if(m_layout != LayoutType::Grid)
			return false;

		const auto columnCount = 128.0f / static_cast<float>(m_grid->getSuggestedItemsPerRow());

		const auto pos = static_cast<int>(static_cast<float>(getWidth()) - m_grid->getItemWidth() * columnCount - static_cast<float>(m_resizerBarB.getWidth()));

		m_stretchableManager.setItemPosition(3, pos - 1);	// prevent rounding issues

		resized();

		return true;
	}

	void PatchManagerUiJuce::addSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		const auto oldCount = m_selectedItems[_tree].size();
		m_selectedItems[_tree].insert(_item);
		const auto newCount = m_selectedItems[_tree].size();
		if(newCount > oldCount)
			onSelectedItemsChanged();
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef - wrong
	void PatchManagerUiJuce::removeSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		const auto it = m_selectedItems.find(_tree);
		if(it == m_selectedItems.end())
			return;
		if(!it->second.erase(_item))
			return;
		onSelectedItemsChanged();
	}

	juce::Colour PatchManagerUiJuce::getResizerBarColor() const
	{
		return m_treeDS->findColour(juce::TreeView::ColourIds::selectedItemBackgroundColourId);
	}

	void PatchManagerUiJuce::setSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		m_selectedItems[_tree] = std::set{_item};

		if(_tree == m_treeDS)
			m_treeTags->onParentSearchChanged(_item->getSearchRequest());

		onSelectedItemsChanged();
	}

	bool PatchManagerUiJuce::setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds)
	{
		if(auto* item = m_treeDS->getItem(*_ds))
		{
			selectTreeItem(item);
			return true;
		}
		return false;
	}

	void PatchManagerUiJuce::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
//		m_info->setPatch(_patch);
	}

	void PatchManagerUiJuce::setSelectedPatches(const std::set<pluginLib::patchDB::PatchPtr>& _patches)
	{
		getListModel()->setSelectedPatches(_patches);
	}

	bool PatchManagerUiJuce::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches)
	{
		return getListModel()->setSelectedPatches(_patches);
	}

	void PatchManagerUiJuce::setCustomSearch(pluginLib::patchDB::SearchHandle _sh)
	{
		m_treeDS->clearSelectedItems();
		m_treeTags->clearSelectedItems();

		getListModel()->setContent(_sh);
	}

	void PatchManagerUiJuce::bringToFront()
	{
		getEditor().selectTabWithComponent(this);
	}

	void PatchManagerUiJuce::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		m_treeDS->processDirty(_dirty);
		m_treeTags->processDirty(_dirty);
		getListModel()->processDirty(_dirty);
	}

	void PatchManagerUiJuce::onSelectedItemsChanged()
	{
		// trees emit onSelectionChanged in destructor, be sure to guard it 
		if(!getListModel())
			return;

		const auto selectedTags = m_selectedItems[m_treeTags];

		auto selectItem = [&](const TreeItem* _item)
		{
			if(_item->getSearchHandle() != pluginLib::patchDB::g_invalidSearchHandle)
			{
				getListModel()->setContent(_item->getSearchHandle());
				return true;
			}
			return false;
		};

		if(!selectedTags.empty())
		{
			if(selectedTags.size() == 1)
			{
				if(selectItem(*selectedTags.begin()))
					return;
			}
			else
			{
				pluginLib::patchDB::SearchRequest search = (*selectedTags.begin())->getSearchRequest();
				for (const auto& selectedTag : selectedTags)
					search.tags.add(selectedTag->getSearchRequest().tags);
				getListModel()->setContent(std::move(search));
				return;
			}
		}

		const auto selectedDataSources = m_selectedItems[m_treeDS];

		if(!selectedDataSources.empty())
		{
			const auto* item = *selectedDataSources.begin();
			selectItem(item);
		}
	}

	void PatchManagerUiJuce::changeListenerCallback(juce::ChangeBroadcaster* _source)
	{
		auto* cs = dynamic_cast<juce::ColourSelector*>(_source);

		if(cs)
		{
			const auto tagType = static_cast<pluginLib::patchDB::TagType>(static_cast<int>(cs->getProperties()["tagType"]));
			const auto tag = cs->getProperties()["tag"].toString().toStdString();

			if(tagType != pluginLib::patchDB::TagType::Invalid && !tag.empty())
			{
				const auto color = cs->getCurrentColour();
				getDB().setTagColor(tagType, tag, color.getARGB());

				repaint();
			}
		}
	}

	void PatchManagerUiJuce::selectTreeItem(TreeItem* _item)
	{
		if(!_item)
			return;

		_item->setSelected(true, true);

		auto* parent = _item->getParentItem();
		while(parent)
		{
			parent->setOpen(true);
			parent = parent->getParentItem();
		}

		_item->getOwnerView()->scrollToKeepItemVisible(_item);
	}

	pluginLib::patchDB::DataSourceNodePtr PatchManagerUiJuce::getSelectedDataSource() const
	{
		const auto* item = dynamic_cast<DatasourceTreeItem*>(m_treeDS->getSelectedItem(0));
		if(!item)
			return {};
		return item->getDataSource();
	}

	TreeItem* PatchManagerUiJuce::getSelectedDataSourceTreeItem() const
	{
		if (!m_treeDS)
			return nullptr;
		auto ds = getSelectedDataSource();
		if (!ds)
			return nullptr;
		return m_treeDS->getItem(*ds);
	}

	pluginLib::patchDB::Color PatchManagerUiJuce::getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		// we want to prevent that a whole list is colored with one color just because that list is based on a tag, prefer other tags instead
		pluginLib::patchDB::TypedTags ignoreTags;

		for (const auto& selectedItem : m_selectedItems)
		{
			for (const auto& item : selectedItem.second)
			{
				const auto& s = item->getSearchRequest();
				ignoreTags.add(s.tags);
			}
		}
		return getDB().getPatchColor(_patch, ignoreTags);
	}

	pluginLib::patchDB::SearchHandle PatchManagerUiJuce::getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem)
	{
		if(auto* item = m_treeDS->getItem(_ds))
		{
			const auto searchHandle = item->getSearchHandle();

			// select the tree item that contains the data source and expand all parents to make it visible
			if(_selectTreeItem)
			{
				selectTreeItem(item);
			}

			return searchHandle;
		}
		return pluginLib::patchDB::g_invalidSearchHandle;
	}

	bool PatchManagerUiJuce::createTag(const GroupType _group, const std::string& _name)
	{
		if(m_treeTags->getItem(_group))
			return false;
		m_treeTags->addGroup(_group, _name);
		return true;
	}
}
