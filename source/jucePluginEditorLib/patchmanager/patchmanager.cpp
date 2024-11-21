#include "patchmanager.h"

#include "datasourcetree.h"
#include "datasourcetreeitem.h"
#include "grid.h"
#include "info.h"
#include "list.h"
#include "listmodel.h"
#include "searchlist.h"
#include "searchtree.h"
#include "status.h"
#include "tagstree.h"
#include "tree.h"

#include "../pluginEditor.h"
#include "../pluginProcessor.h"

#include "jucePluginLib/types.h"
#include "jucePluginLib/clipboard.h"

#include "dsp56kEmu/logging.h"

#if JUCE_MAJOR_VERSION < 8	// they forgot this include but fixed it in version 8+
#include "juce_gui_extra/misc/juce_ColourSelector.h"
#endif

namespace jucePluginEditorLib::patchManager
{
	constexpr int g_scale = 2;
	constexpr auto g_searchBarHeight = 32;
	constexpr int g_padding = 4;

	PatchManager::PatchManager(Editor& _editor, Component* _root, const std::initializer_list<GroupType>& _groupTypes)
	: DB(juce::File(_editor.getProcessor().getPatchManagerDataFolder(false)))
	, m_editor(_editor)
	, m_state(*this)
	{
		setTagTypeName(pluginLib::patchDB::TagType::Category, "Category");
		setTagTypeName(pluginLib::patchDB::TagType::Tag, "Tag");
		setTagTypeName(pluginLib::patchDB::TagType::Favourites, "Favourite");

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

		m_searchTreeDS = new SearchTree(*m_treeDS);
		m_searchTreeDS->setSize(m_treeDS->getWidth(), g_searchBarHeight);
		m_searchTreeDS->setTopLeftPosition(m_treeDS->getX(), m_treeDS->getHeight() + g_padding);

		addAndMakeVisible(m_treeDS);
		addAndMakeVisible(m_searchTreeDS);

		// 2nd column
		w = weight(20);
		m_treeTags = new TagsTree(*this);
		m_treeTags->setTopLeftPosition(m_treeDS->getRight() + g_padding, 0);
		m_treeTags->setSize(w - g_padding, rootH - g_searchBarHeight - g_padding);

		m_searchTreeTags = new SearchTree(*m_treeTags);
		m_searchTreeTags->setTopLeftPosition(m_treeTags->getX(), m_treeTags->getHeight() + g_padding);
		m_searchTreeTags->setSize(m_treeTags->getWidth(), g_searchBarHeight);

		addAndMakeVisible(m_treeTags);
		addAndMakeVisible(m_searchTreeTags);

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

		m_searchList = new SearchList(*m_list);
		m_searchList->setTopLeftPosition(m_list->getX(), m_list->getHeight() + g_padding);
		m_searchList->setSize(m_list->getWidth(), g_searchBarHeight);

		addAndMakeVisible(m_list);
		addChildComponent(m_grid);
		addAndMakeVisible(m_searchList);

		// 4th column
		m_info = new Info(*this);
		m_info->setTopLeftPosition(m_list->getRight() + g_padding, 0);
		m_info->setSize(getWidth() - m_info->getX(), rootH - g_searchBarHeight - g_padding);

		m_status = new Status();
		m_status->setTopLeftPosition(m_info->getX(), m_info->getHeight() + g_padding);
		m_status->setSize(m_info->getWidth(), g_searchBarHeight);

		addAndMakeVisible(m_info);
		addAndMakeVisible(m_status);

		if(const auto t = getTemplate("pm_search"))
		{
			t->apply(getEditor(), *m_searchList);
			t->apply(getEditor(), *m_searchTreeDS);
			t->apply(getEditor(), *m_searchTreeTags);
		}

		m_searchList->setTextToShowWhenEmpty("Search...", m_searchList->findColour(juce::TextEditor::textColourId).withAlpha(0.5f));
		m_searchTreeDS->setTextToShowWhenEmpty("Search...", m_searchTreeDS->findColour(juce::TextEditor::textColourId).withAlpha(0.5f));
		m_searchTreeTags->setTextToShowWhenEmpty("Search...", m_searchTreeTags->findColour(juce::TextEditor::textColourId).withAlpha(0.5f));

		if(const auto t = getTemplate("pm_status_label"))
		{
			t->apply(getEditor(), *m_status);
		}

		juce::StretchableLayoutManager lm;

		m_stretchableManager.setItemLayout(0, 100, rootW * 0.5, m_treeDS->getWidth());		m_stretchableManager.setItemLayout(1, 5, 5, 5);
		m_stretchableManager.setItemLayout(2, 100, rootW * 0.5, m_treeTags->getWidth());	m_stretchableManager.setItemLayout(3, 5, 5, 5);
		m_stretchableManager.setItemLayout(4, 100, rootW * 0.5, m_list->getWidth());		m_stretchableManager.setItemLayout(5, 5, 5, 5);
		m_stretchableManager.setItemLayout(6, 100, rootW * 0.5, m_info->getWidth());

		m_resizerBarA.setSize(5, rootH);
		m_resizerBarB.setSize(5, rootH);
		m_resizerBarC.setSize(5, rootH);

		addAndMakeVisible(m_resizerBarA);
		addAndMakeVisible(m_resizerBarB);
		addAndMakeVisible(m_resizerBarC);

		PatchManager::resized();

		const auto& config = m_editor.getProcessor().getConfig();

		if(config.getIntValue("pm_layout", static_cast<int>(LayoutType::List)) == static_cast<int>(LayoutType::Grid))
			setLayout(LayoutType::Grid);
		else
			PatchManager::resized();

		startTimer(200);
	}

	PatchManager::~PatchManager()
	{
		stopTimer();

		delete m_status;
		delete m_info;
		delete m_searchList;
		delete m_list;
		delete m_grid;

		// trees emit onSelectionChanged, be sure to guard it 
		m_list = nullptr;
		m_grid = nullptr;

		delete m_searchTreeTags;
		delete m_treeTags;
		delete m_searchTreeDS;
		delete m_treeDS;
	}

	void PatchManager::timerCallback()
	{
		uiProcess();
	}

	void PatchManager::processDirty(const pluginLib::patchDB::Dirty& _dirty) const
	{
		m_treeDS->processDirty(_dirty);
		m_treeTags->processDirty(_dirty);
		getListModel()->processDirty(_dirty);

		m_status->setScanning(isScanning());

		m_info->processDirty(_dirty);

		if(!_dirty.errors.empty())
		{
			std::string msg = "Patch Manager encountered errors:\n\n";
			for(size_t i=0; i<_dirty.errors.size(); ++i)
			{
				msg += _dirty.errors[i];
				if(i < _dirty.errors.size() - 1)
					msg += "\n";
			}

			juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, "Patch Manager Error", msg);
		}
	}

	void PatchManager::setSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		m_selectedItems[_tree] = std::set{_item};

		if(_tree == m_treeDS)
			m_treeTags->onParentSearchChanged(_item->getSearchRequest());

		onSelectedItemsChanged();
	}

	void PatchManager::addSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		const auto oldCount = m_selectedItems[_tree].size();
		m_selectedItems[_tree].insert(_item);
		const auto newCount = m_selectedItems[_tree].size();
		if(newCount > oldCount)
			onSelectedItemsChanged();
	}

	// ReSharper disable once CppParameterMayBeConstPtrOrRef - wrong
	void PatchManager::removeSelectedItem(Tree* _tree, const TreeItem* _item)
	{
		const auto it = m_selectedItems.find(_tree);
		if(it == m_selectedItems.end())
			return;
		if(!it->second.erase(_item))
			return;
		onSelectedItemsChanged();
	}

	bool PatchManager::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::SearchHandle _fromSearch)
	{
		return setSelectedPatch(getCurrentPart(), _patch, _fromSearch);
	}

	void PatchManager::setLayout(const LayoutType _layout)
	{
		if(m_layout == _layout)
			return;

		auto* oldModel = getListModel();

		m_layout = _layout;

		auto* newModel = getListModel();

		m_searchList->setListModel(newModel);

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

		auto& config = m_editor.getProcessor().getConfig();
		config.setValue("pm_layout", static_cast<int>(_layout));
		config.saveIfNeeded();
	}

	bool PatchManager::setGridLayout128()
	{
		if(m_layout != LayoutType::Grid)
			return false;

		const auto columnCount = 128.0f / static_cast<float>(m_grid->getSuggestedItemsPerRow());

		const auto pos = static_cast<int>(static_cast<float>(getWidth()) - m_grid->getItemWidth() * columnCount - static_cast<float>(m_resizerBarB.getWidth()));

		m_stretchableManager.setItemPosition(3, pos - 1);	// prevent rounding issues

		resized();

		return true;
	}

	void PatchManager::setCustomSearch(const pluginLib::patchDB::SearchHandle _sh) const
	{
		m_treeDS->clearSelectedItems();
		m_treeTags->clearSelectedItems();

		getListModel()->setContent(_sh);
	}

	void PatchManager::bringToFront() const
	{
		m_editor.selectTabWithComponent(this);
	}

	bool PatchManager::selectPatch(const uint32_t _part, const int _offset)
	{
		auto [patch, _] = m_state.getNeighbourPreset(_part, _offset);

		if(!patch)
			return false;

		if(!setSelectedPatch(_part, patch, m_state.getSearchHandle(_part)))
			return false;

		if(_part == getCurrentPart())
			getListModel()->setSelectedPatches({patch});

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch)
	{
		if(!activatePatch(_patch, _part))
			return false;

		m_state.setSelectedPatch(_part, pluginLib::patchDB::PatchKey(*_patch), _fromSearch);

		if(_part == getCurrentPart())
			m_info->setPatch(_patch);

		return true;
	}

	bool PatchManager::setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds) const
	{
		if(auto* item = m_treeDS->getItem(*_ds))
		{
			selectTreeItem(item);
			return true;
		}
		return false;
	}

	pluginLib::patchDB::DataSourceNodePtr PatchManager::getSelectedDataSource() const
	{
		const auto* item = dynamic_cast<DatasourceTreeItem*>(m_treeDS->getSelectedItem(0));
		if(!item)
			return {};
		return item->getDataSource();
	}

	TreeItem* PatchManager::getSelectedDataSourceTreeItem() const
	{
		if (!m_treeDS)
			return nullptr;
		auto ds = getSelectedDataSource();
		if (!ds)
			return nullptr;
		return m_treeDS->getItem(*ds);
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return false;

		const auto patchDs = _patch->source.lock();

		if(!patchDs)
			return false;

		if(!setSelectedPatch(_part, pluginLib::patchDB::PatchKey(*_patch)))
			return false;

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchKey& _patch)
	{
		// we've got a patch, but we do not know its search handle, i.e. which list it is part of, find the missing information

		if(!_patch.isValid())
			return false;

		const auto searchHandle = getSearchHandle(*_patch.source, _part == getCurrentPart());

		if(searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return false;

		m_state.setSelectedPatch(_part, _patch, searchHandle);

		if(getCurrentPart() == _part)
			getListModel()->setSelectedPatches({_patch});

		onSelectedPatchChanged(_part, _patch);

		return true;
	}

	void PatchManager::copyPatchesToLocalStorage(const pluginLib::patchDB::DataSourceNodePtr& _ds, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, int _part)
	{
		copyPatchesTo(_ds, _patches, -1, [this, _part](const std::vector<pluginLib::patchDB::PatchPtr>& _savedPatches)
		{
			if(_part == -1)
				return;

			juce::MessageManager::callAsync([this, _part, _savedPatches]
			{
				setSelectedPatch(_part, _savedPatches.front());
			});
		});
	}

	uint32_t PatchManager::createSaveMenuEntries(juce::PopupMenu& _menu, uint32_t _part, const std::string& _name/* = "patch"*/, uint64_t _userData/* = 0*/)
	{
		const auto& state = getState();
		const auto key = state.getPatch(_part);

		uint32_t countAdded = 0;

		if(key.isValid() && key.source->type == pluginLib::patchDB::SourceType::LocalStorage)
		{
			// the key that is stored in the state might not contain patches, find the real data source in the DB
			const auto ds = getDataSource(*key.source);

			if(ds)
			{
				if(const auto p = ds->getPatch(key))
				{
					if(*p == key)
					{
						++countAdded;
						_menu.addItem("Overwrite " + _name + " '" + p->getName() + "' in user bank '" + ds->name + "'", true, false, [this, p, _part, _userData]
						{
							const auto newPatch = requestPatchForPart(_part, _userData);
							if(newPatch)
							{
								replacePatch(p, newPatch);
							}
						});
					}
				}
			}
		}

		const auto existingLocalDS = getDataSourcesOfSourceType(pluginLib::patchDB::SourceType::LocalStorage);

		if(!existingLocalDS.empty())
		{
			if(countAdded)
				_menu.addSeparator();

			for (const auto& ds : existingLocalDS)
			{
				++countAdded;
				_menu.addItem("Add " + _name + " to user bank '" + ds->name + "'", true, false, [this, ds, _part, _userData]
				{
					const auto newPatch = requestPatchForPart(_part, _userData);

					if(!newPatch)
						return;

					copyPatchesToLocalStorage(ds, {newPatch}, static_cast<int>(_part));
				});
			}
		}
		else
		{
			++countAdded;
			_menu.addItem("Create new user bank and add " + _name, true, false, [this, _part, _userData]
			{
				const auto newPatch = requestPatchForPart(_part, _userData);

				if(!newPatch)
					return;

				pluginLib::patchDB::DataSource ds;

				ds.name = "User Bank";
				ds.type = pluginLib::patchDB::SourceType::LocalStorage;
				ds.origin = pluginLib::patchDB::DataSourceOrigin::Manual;
				ds.timestamp = std::chrono::system_clock::now();
				addDataSource(ds, false, [newPatch, _part, this](const bool _success, const std::shared_ptr<pluginLib::patchDB::DataSourceNode>& _ds)
				{
					if(_success)
						copyPatchesToLocalStorage(_ds, {newPatch}, static_cast<int>(_part));
				});
			});
		}

		return countAdded;
	}

	std::string PatchManager::getTagTypeName(const pluginLib::patchDB::TagType _type) const
	{
		const auto it = m_tagTypeNames.find(_type);
		if(it == m_tagTypeNames.end())
		{
			return {};
		}
		return it->second;
	}

	void PatchManager::setTagTypeName(const pluginLib::patchDB::TagType _type, const std::string& _name)
	{
		if(_name.empty())
		{
			m_tagTypeNames.erase(_type);
			return;
		}

		m_tagTypeNames[_type] = _name;
	}

	bool PatchManager::selectPrevPreset(const uint32_t _part)
	{
		return selectPatch(_part, -1);
	}

	bool PatchManager::selectNextPreset(const uint32_t _part)
	{
		return selectPatch(_part, 1);
	}

	bool PatchManager::selectPatch(const uint32_t _part, const pluginLib::patchDB::DataSource& _ds, const uint32_t _program)
	{
		const auto searchHandle = getSearchHandle(_ds, _part == getCurrentPart());

		if(searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return false;

		const auto s = getSearch(searchHandle);
		if(!s)
			return false;

		pluginLib::patchDB::PatchPtr p;

		std::shared_lock lockResults(s->resultsMutex);
		for (const auto& patch : s->results)
		{
			if(patch->program == _program)
			{
				p = patch;
				break;
			}
		}

		if(!p)
			return false;

		if(!activatePatch(p, _part))
			return false;

		setSelectedPatch(_part, p, s->handle);

		if(_part == getCurrentPart())
			getListModel()->setSelectedPatches({p});

		return true;
	}

	void PatchManager::setListStatus(uint32_t _selected, uint32_t _total) const
	{
		m_status->setListStatus(_selected, _total);
	}

	pluginLib::patchDB::Color PatchManager::getPatchColor(const pluginLib::patchDB::PatchPtr& _patch) const
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
		return DB::getPatchColor(_patch, ignoreTags);
	}

	bool PatchManager::addGroupTreeItemForTag(const pluginLib::patchDB::TagType _type) const
	{
		return addGroupTreeItemForTag(_type, getTagTypeName(_type));
	}

	bool PatchManager::addGroupTreeItemForTag(const pluginLib::patchDB::TagType _type, const std::string& _name) const
	{
		const auto groupType = toGroupType(_type);
		if(groupType == GroupType::Invalid)
			return false;
		if(_name.empty())
			return false;
		if(m_treeTags->getItem(groupType))
			return false;
		m_treeTags->addGroup(groupType, _name);
		return true;
	}

	void PatchManager::paint(juce::Graphics& g)
	{
		g.fillAll(juce::Colour(0,0,0));
	}

	void PatchManager::exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, FileType _fileType) const
	{
#if SYNTHLIB_DEMO_MODE
		getEditor().showDemoRestrictionMessageBox();
#else
		FileType type = _fileType;
		const auto name = Editor::createValidFilename(type, _file);

		std::vector<pluginLib::patchDB::Data> patchData;
		for (const auto& patch : _patches)
		{
			const auto patchSysex = prepareSave(patch);

			if(!patchSysex.empty())
				patchData.push_back(patchSysex);
		}

		if(!Editor::savePresets(type, name, patchData))
			juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, "Save failed", "Failed to write data to " + _file.getFullPathName().toStdString());
#endif
	}

	bool PatchManager::exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, FileType _fileType) const
	{
		if(_patches.size() > 128)
		{
			if(1 != juce::NativeMessageBox::showOkCancelBox(juce::AlertWindow::WarningIcon, 
				"Patch Manager",
				"You are trying to export more than 128 presets into a single file. Note that this dump exceeds the size of one bank and may not be compatible with your hardware"))
				return true;
		}

		ListModel::sortPatches(_patches, pluginLib::patchDB::SourceType::LocalStorage);

		getEditor().savePreset([this, p = std::move(_patches), _fileType](const juce::File& _file)
		{
			exportPresets(_file, p, _fileType);
		});

		return true;
	}

	void PatchManager::resized()
	{
		if(!m_treeDS)
			return;

		m_info->setVisible(m_layout == LayoutType::List);
		m_resizerBarC.setVisible(m_layout == LayoutType::List);

		std::vector<Component*> comps = {m_treeDS, &m_resizerBarA, m_treeTags, &m_resizerBarB, dynamic_cast<juce::Component*>(getListModel())};
		if(m_layout == LayoutType::List)
		{
			comps.push_back(&m_resizerBarC);
			comps.push_back(m_info);
		}

		m_stretchableManager.layOutComponents(comps.data(), static_cast<int>(comps.size()), 0, 0, getWidth(), getHeight(), false, false);

		auto layoutXAxis = [](Component* _target, const Component* _source)
		{
			_target->setTopLeftPosition(_source->getX(), _target->getY());
			_target->setSize(_source->getWidth(), _target->getHeight());
		};

		layoutXAxis(m_searchTreeDS, m_treeDS);
		layoutXAxis(m_searchTreeTags, m_treeTags);

		if(m_layout == LayoutType::List)
		{
			layoutXAxis(m_searchList, dynamic_cast<Component*>(getListModel()));
			layoutXAxis(m_status, m_info);
		}
		else
		{
			m_searchList->setTopLeftPosition(m_grid->getX(), m_searchList->getY());
			m_searchList->setSize(m_grid->getWidth()/3 - g_padding, m_searchList->getHeight());

			m_status->setTopLeftPosition(m_searchList->getX() + m_searchList->getWidth() + g_padding, m_searchList->getY());
			m_status->setSize(m_grid->getWidth()/3*2, m_status->getHeight());
		}
	}

	juce::Colour PatchManager::getResizerBarColor() const
	{
		return m_treeDS->findColour(juce::TreeView::ColourIds::selectedItemBackgroundColourId);
	}

	bool PatchManager::copyPart(const uint8_t _target, const uint8_t _source)
	{
		if(_target == _source)
			return false;

		const auto source = requestPatchForPart(_source);
		if(!source)
			return false;

		if(!activatePatch(source, _target))
			return false;

		m_state.copy(_target, _source);

		if(getCurrentPart() == _target)
			setSelectedPatch(_target, m_state.getPatch(_target));

		return true;
	}

	std::shared_ptr<genericUI::UiObject> PatchManager::getTemplate(const std::string& _name) const
	{
		return m_editor.getTemplate(_name);
	}

	bool PatchManager::activatePatch(const std::string& _filename, const uint32_t _part)
	{
		if(_part >= m_state.getPartCount() || _part > m_editor.getProcessor().getController().getPartCount())
			return false;

		const auto patches = loadPatchesFromFiles(std::vector<std::string>{_filename});

		if(patches.empty())
			return false;

		const auto& patch = patches.front();

		if(!activatePatch(patch, _part))
			return false;

		if(getCurrentPart() == _part)
			getListModel()->setSelectedPatches(std::set<pluginLib::patchDB::PatchKey>{});

		return true;
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::loadPatchesFromFiles(const juce::StringArray& _files)
	{
		std::vector<std::string> files;

		for (const auto& file : _files)
			files.push_back(file.toStdString());

		return loadPatchesFromFiles(files);
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::loadPatchesFromFiles(const std::vector<std::string>& _files)
	{
		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& file : _files)
		{
			pluginLib::patchDB::DataList results;
			if(!loadFile(results, file) || results.empty())
				continue;

			for (auto& result : results)
			{
				if(const auto patch = initializePatch(std::move(result)))
					patches.push_back(patch);
			}
		}
		return patches;
	}

	void PatchManager::onLoadFinished()
	{
		DB::onLoadFinished();

		for(uint32_t i=0; i<std::min(m_editor.getProcessor().getController().getPartCount(), static_cast<uint8_t>(m_state.getPartCount())); ++i)
		{
			const auto p = m_state.getPatch(i);

			// If the state has been deserialized, the patch key is valid but the search handle is not. Only restore if that is the case
			if(p.isValid() && m_state.getSearchHandle(i) == pluginLib::patchDB::g_invalidSearchHandle)
			{
				if(!setSelectedPatch(i, p))
					m_state.clear(i);
			}
			else if(!m_state.isValid(i))
			{
				// otherwise, try to restore from the currently loaded patch
				updateStateAsync(i, requestPatchForPart(i));
			}
		}
	}

	void PatchManager::setPerInstanceConfig(const std::vector<uint8_t>& _data)
	{
		if(_data.empty())
			return;
		try
		{
			pluginLib::PluginStream s(_data);
			const auto version = s.read<uint32_t>();
			if(version != 1)
				return;
			m_state.setConfig(s);
		}
		catch(std::range_error& e)
		{
			LOG("Failed to to load per instance config: " << e.what());
		}
	}

	void PatchManager::getPerInstanceConfig(std::vector<uint8_t>& _data) const
	{
		pluginLib::PluginStream s;
		s.write<uint32_t>(1);	// version
		m_state.getConfig(s);
		s.toVector(_data);
	}

	void PatchManager::onProgramChanged(const uint32_t _part)
	{
		if(isLoading())
			return;
		return;
		pluginLib::patchDB::Data data;
		if(!requestPatchForPart(data, _part, 0))
			return;
		const auto patch = initializePatch(std::move(data));
		if(!patch)
			return;
		updateStateAsync(_part, patch);
	}

	void PatchManager::setCurrentPart(uint32_t _part)
	{
		if(!m_state.isValid(_part))
			return;

		setSelectedPatch(_part, m_state.getPatch(_part));
	}

	void PatchManager::updateStateAsync(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return;

		const auto patchDs = _patch->source.lock();

		if(patchDs)
		{
			setSelectedPatch(_part, _patch);
			return;
		}

		// we've got a patch, but we do not know its datasource and search handle, find the data source by executing a search

		findDatasourceForPatch(_patch, [this, _part](const pluginLib::patchDB::Search& _search)
		{
			const auto handle = _search.handle;

			std::vector<pluginLib::patchDB::PatchPtr> results;
			results.assign(_search.results.begin(), _search.results.end());

			if(results.empty())
				return;

			if(results.size() > 1)
			{
				// if there are multiple results, sort them, we prefer ROM results over other results

				std::sort(results.begin(), results.end(), [](const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b)
				{
					const auto dsA = _a->source.lock();
					const auto dsB = _b->source.lock();

					if(!dsA || !dsB)
						return true;

					if(dsA->type < dsB->type)
						return true;
					if(dsA->type > dsB->type)
						return false;
					if(dsA->name < dsB->name)
						return true;
					if(dsA->name > dsB->name)
						return false;
					if(_a->program < _b->program)
						return true;
					return false;
				});
			}

			const auto currentPatch = results.front();

			const auto key = pluginLib::patchDB::PatchKey(*currentPatch);

			runOnUiThread([this, _part, key, handle]
			{
				cancelSearch(handle);
				setSelectedPatch(_part, key);
			});
		});
	}

	ListModel* PatchManager::getListModel() const
	{
		if(m_layout == LayoutType::List)
			return m_list;
		return m_grid;
	}

	void PatchManager::startLoaderThread(const juce::File& _migrateFromDir/* = {}*/)
	{
		if(_migrateFromDir.getFullPathName().isEmpty())
		{
			const auto& configOptions = m_editor.getProcessor().getConfigOptions();
			DB::startLoaderThread(configOptions.getDefaultFile().getParentDirectory());
			return;
		}
		DB::startLoaderThread(_migrateFromDir);
	}

	pluginLib::patchDB::SearchHandle PatchManager::getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem)
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

		const auto search = getSearch(_ds);

		if(!search)
			return pluginLib::patchDB::g_invalidSearchHandle;

		return search->handle;
	}

	void PatchManager::onSelectedItemsChanged()
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

	void PatchManager::changeListenerCallback(juce::ChangeBroadcaster* _source)
	{
		auto* cs = dynamic_cast<juce::ColourSelector*>(_source);

		if(cs)
		{
			const auto tagType = static_cast<pluginLib::patchDB::TagType>(static_cast<int>(cs->getProperties()["tagType"]));
			const auto tag = cs->getProperties()["tag"].toString().toStdString();

			if(tagType != pluginLib::patchDB::TagType::Invalid && !tag.empty())
			{
				const auto color = cs->getCurrentColour();
				setTagColor(tagType, tag, color.getARGB());

				repaint();
			}
		}
	}

	void PatchManager::selectTreeItem(TreeItem* _item)
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

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::getPatchesFromString(const std::string& _text)
	{
		auto data = pluginLib::Clipboard::getDataFromString(m_editor.getProcessor(), _text);

		if(data.sysex.empty())
			return {};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (auto& result : data.sysex)
		{
			if(const auto patch = initializePatch(std::move(result)))
				patches.push_back(patch);
		}

		return patches;
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::getPatchesFromClipboard()
	{
		return getPatchesFromString(juce::SystemClipboard::getTextFromClipboard().toStdString());
	}

	bool PatchManager::activatePatchFromString(const std::string& _text)
	{
		const auto patches = getPatchesFromString(_text);

		if(patches.size() != 1)
			return false;

		return activatePatch(patches.front(), getCurrentPart());
	}

	bool PatchManager::activatePatchFromClipboard()
	{
		return activatePatchFromString(juce::SystemClipboard::getTextFromClipboard().toStdString());
	}

	std::string PatchManager::toString(const pluginLib::patchDB::PatchPtr& _patch) const
	{
		if(!_patch)
			return {};

		const auto data = prepareSave(_patch);

		return pluginLib::Clipboard::createJsonString(m_editor.getProcessor(), {}, {}, data);
	}
}
