#include "listmodel.h"

#include "defaultskin.h"
#include "listitem.h"
#include "patchmanager.h"
#include "savepatchdesc.h"
#include "search.h"

#include "../pluginEditor.h"

#include "juceUiLib/uiObjectStyle.h"

namespace jucePluginEditorLib::patchManager
{
	ListModel::ListModel(PatchManager& _pm): m_patchManager(_pm)
	{
	}

	void ListModel::setContent(const pluginLib::patchDB::SearchHandle& _handle)
	{
		cancelSearch();

		const auto& search = m_patchManager.getSearch(_handle);

		if (!search)
			return;

		setContent(search);
	}

	void ListModel::setContent(pluginLib::patchDB::SearchRequest&& _request)
	{
		cancelSearch();
		const auto sh = getPatchManager().search(std::move(_request));
		setContent(sh);
		m_searchHandle = sh;
	}

	void ListModel::clear()
	{
		m_search.reset();
		m_patches.clear();
		m_filteredPatches.clear();
		onModelChanged();
		getPatchManager().setListStatus(0, 0);
	}

	void ListModel::refreshContent()
	{
		setContent(m_search);
	}

	void ListModel::setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search)
	{
		const std::set<Patch> selectedPatches = getSelectedPatches();

		m_search = _search;

		m_patches.clear();
		{
			std::shared_lock lock(_search->resultsMutex);
			m_patches.insert(m_patches.end(), _search->results.begin(), _search->results.end());
		}

		sortPatches();
		filterPatches();

		onModelChanged();

		setSelectedPatches(selectedPatches);

		redraw();

		getPatchManager().setListStatus(static_cast<uint32_t>(selectedPatches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	bool ListModel::exportPresets(const bool _selectedOnly, FileType _fileType) const
	{
		Patches patches;

		if(_selectedOnly)
		{
			const auto selected = getSelectedPatches();
			if(selected.empty())
				return false;
			patches.assign(selected.begin(), selected.end());
		}
		else
		{
			patches = getPatches();
		}

		if(patches.empty())
			return false;

		return getPatchManager().exportPresets(std::move(patches), _fileType);
	}

	bool ListModel::onClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!_mouseEvent.mods.isPopupMenu())
			return false;

		auto fileTypeMenu = [this](const std::function<void(FileType)>& _func)
		{
			juce::PopupMenu menu;
			menu.addItem(".syx", [this, _func]{_func(FileType::Syx);});
			menu.addItem(".mid", [this, _func]{_func(FileType::Mid);});
			return menu;
		};

		auto selectedPatches = getSelectedPatches();

		const auto hasSelectedPatches = !selectedPatches.empty();

		juce::PopupMenu menu;
		if(hasSelectedPatches)
			menu.addSubMenu("Export selected...", fileTypeMenu([this](const FileType _fileType) { exportPresets(true, _fileType); }));
		menu.addSubMenu("Export all...", fileTypeMenu([this](const FileType _fileType) { exportPresets(false, _fileType); }));

		if(hasSelectedPatches)
		{
			menu.addSeparator();

			pluginLib::patchDB::TypedTags tags;

			for (const auto& selectedPatch : selectedPatches)
				tags.add(selectedPatch->getTags());

			if(selectedPatches.size() == 1)
			{
				const auto& patch = *selectedPatches.begin();
				const auto row = getSelectedEntry();
				auto pos = getEntryPosition(row, true);

				pos.setY(pos.getY()-2);
				pos.setHeight(pos.getHeight()+4);

				menu.addItem("Rename...", [this, patch, pos]
				{
					beginEdit(dynamic_cast<juce::Component*>(this), pos, patch->getName(), [this, patch](bool _cond, const std::string& _name)
					{
						if(_name != patch->getName())
							getPatchManager().renamePatch(patch, _name);
					});
				});

				menu.addItem("Locate", [this, patch]
				{
					m_patchManager.setSelectedDataSource(patch->source.lock());
				});
			}

			if(!m_search->request.tags.empty())
			{
				menu.addItem("Remove selected", [this, s = selectedPatches]
				{
					const std::vector<pluginLib::patchDB::PatchPtr> patches(s.begin(), s.end());
					pluginLib::patchDB::TypedTags removeTags;

					// converted "added" tags to "removed" tags
					for (const auto& tags : m_search->request.tags.get())
					{
						const pluginLib::patchDB::TagType type = tags.first;
						const auto& t = tags.second;
							
						for (const auto& tag : t.getAdded())
							removeTags.addRemoved(type, tag);
					}

					m_patchManager.modifyTags(patches, removeTags);
					m_patchManager.repaint();
				});
			}
			else if(getSourceType() == pluginLib::patchDB::SourceType::LocalStorage)
			{
				menu.addItem("Delete selected", [this, s = selectedPatches]
				{
					if(showDeleteConfirmationMessageBox())
					{
						const std::vector<pluginLib::patchDB::PatchPtr> patches(s.begin(), s.end());
						m_patchManager.removePatches(m_search->request.sourceNode, patches);
					}
				});
			}

			if(tags.containsAdded())
			{
				bool haveSeparator = false;

				for (const auto& it : tags.get())
				{
					const auto type = it.first;

					const auto& t = it.second;

					if(t.empty())
						continue;

					const auto tagTypeName = m_patchManager.getTagTypeName(type);

					if(tagTypeName.empty())
						continue;

					juce::PopupMenu tagMenu;

					for (const auto& tag : t.getAdded())
					{
						pluginLib::patchDB::TypedTags removeTags;
						removeTags.addRemoved(type, tag);

						std::vector<pluginLib::patchDB::PatchPtr> patches{selectedPatches.begin(), selectedPatches.end()};

						tagMenu.addItem(tag, [this, s = std::move(patches), removeTags]
						{
							m_patchManager.modifyTags(s, removeTags);
						});
					}

					if(!haveSeparator)
					{
						menu.addSeparator();
						haveSeparator = true;
					}

					menu.addSubMenu("Remove from " + tagTypeName, tagMenu);
				}
			}

			{
				bool haveSeparator = false;

				for(uint32_t i=0; i<static_cast<uint32_t>(pluginLib::patchDB::TagType::Count); ++i)
				{
					const auto type = static_cast<pluginLib::patchDB::TagType>(i);
					std::set<pluginLib::patchDB::Tag> availTags;
					m_patchManager.getTags(type, availTags);

					if(availTags.empty())
						continue;

					const auto tagTypeName = m_patchManager.getTagTypeName(type);

					if(tagTypeName.empty())
						continue;

					juce::PopupMenu tagMenu;

					for (const auto& tag : availTags)
					{
						pluginLib::patchDB::TypedTags addedTags;
						addedTags.add(type, tag);

						std::vector<pluginLib::patchDB::PatchPtr> patches{selectedPatches.begin(), selectedPatches.end()};

						tagMenu.addItem(tag, [this, addedTags, s = std::move(patches)]
						{
							m_patchManager.modifyTags(s, addedTags);
						});
					}

					if(!haveSeparator)
					{
						menu.addSeparator();
						haveSeparator = true;
					}

					menu.addSubMenu("Add to " + tagTypeName, tagMenu);
				}
			}
		}
		menu.addSeparator();
		menu.addItem("Hide duplicates (by hash)", true, m_hideDuplicatesByHash, [this]
		{
			setFilter(m_filter, !m_hideDuplicatesByHash, m_hideDuplicatesByName);
		});
		menu.addItem("Hide duplicates (by name)", true, m_hideDuplicatesByName, [this]
		{
			setFilter(m_filter, m_hideDuplicatesByHash, !m_hideDuplicatesByName);
		});

		menu.addSeparator();

		juce::PopupMenu layoutMenu;
		layoutMenu.addItem("List + Info", true, m_patchManager.getLayout() == PatchManager::LayoutType::List, [this]
		{
			m_patchManager.setLayout(PatchManager::LayoutType::List);
		});
		layoutMenu.addItem("Grid", true, m_patchManager.getLayout() == PatchManager::LayoutType::Grid, [this]
		{
			m_patchManager.setLayout(PatchManager::LayoutType::Grid);
		});
		menu.addSubMenu("Layout", layoutMenu);

		menu.showMenuAsync({});

		return true;
	}

	void ListModel::cancelSearch()
	{
		if(m_searchHandle == pluginLib::patchDB::g_invalidSearchHandle)
			return;
		getPatchManager().cancelSearch(m_searchHandle);
		m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	}

	int ListModel::getNumRows()
	{
		return static_cast<int>(getPatches().size());
	}

	void ListModel::paintListBoxItem(const int _rowNumber, juce::Graphics& _g, const int _width, const int _height, const bool _rowIsSelected)
	{
		const auto* style = dynamic_cast<const genericUI::UiObjectStyle*>(&getStyle());

		if (_rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		const auto& patch = getPatch(_rowNumber);

		const auto text = patch->getName();

		if(_rowIsSelected)
		{
			if(style)
				_g.setColour(style->getSelectedItemBackgroundColor());
			else
				_g.setColour(juce::Colour(0x33ffffff));
			_g.fillRect(0, 0, _width, _height);
		}

		if (style)
		{
			if (const auto f = style->getFont())
				_g.setFont(*f);
		}

		const auto c = getPatchManager().getPatchColor(patch);

		constexpr int offsetX = 20;

		if(c != pluginLib::patchDB::g_invalidColor)
		{
			_g.setColour(juce::Colour(c));
			constexpr auto s = 8.f;
			constexpr auto sd2 = 0.5f * s;
			_g.fillEllipse(10 - sd2, static_cast<float>(_height) * 0.5f - sd2, s, s);
//			_g.setColour(juce::Colour(0xffffffff));
//			_g.drawEllipse(10 - sd2, static_cast<float>(_height) * 0.5f - sd2, s, s, 1.0f);
//			offsetX += 14;
		}

//		if(c != pluginLib::patchDB::g_invalidColor)
//			_g.setColour(juce::Colour(c));
//		else
		_g.setColour(findColor(juce::ListBox::textColourId));

		_g.drawText(text, offsetX, 0, _width - 4, _height, style ? style->getAlign() : juce::Justification::centredLeft, true);
	}

	juce::var ListModel::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
	{
		const auto& ranges = rowsToDescribe.getRanges();

		if (ranges.isEmpty())
			return {};

		std::map<uint32_t, pluginLib::patchDB::PatchPtr> patches;

		for (const auto& range : ranges)
		{
			for (int i = range.getStart(); i < range.getEnd(); ++i)
			{
				if(i >= 0 && static_cast<size_t>(i) < getPatches().size())
					patches.insert({i, getPatches()[i]});
			}
		}

		return new SavePatchDesc(m_patchManager, std::move(patches));
	}

	juce::Component* ListModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate)
	{
		auto* existing = dynamic_cast<ListItem*>(existingComponentToUpdate);

		if (existing)
		{
			existing->setRow(rowNumber);
			return existing;
		}

		delete existingComponentToUpdate;

		return new ListItem(*this, rowNumber);
	}

	void ListModel::selectedRowsChanged(const int lastRowSelected)
	{
		ListBoxModel::selectedRowsChanged(lastRowSelected);

		if(!m_ignoreSelectedRowsChanged)
			activateSelectedPatch();

		const auto patches = getSelectedPatches();
		getPatchManager().setListStatus(static_cast<uint32_t>(patches.size()), static_cast<uint32_t>(getPatches().size()));
	}

	std::set<ListModel::Patch> ListModel::getSelectedPatches() const
	{
		std::set<Patch> result;

		const auto selectedRows = getSelectedEntries();
		const auto& ranges = selectedRows.getRanges();

		for (const auto& range : ranges)
		{
			for (int i = range.getStart(); i < range.getEnd(); ++i)
			{
				if (i >= 0 && static_cast<size_t>(i) < getPatches().size())
					result.insert(getPatch(i));
			}
		}
		return result;
	}

	bool ListModel::setSelectedPatches(const std::set<Patch>& _patches)
	{
		if (_patches.empty())
			return false;

		std::set<pluginLib::patchDB::PatchKey> patches;

		for (const auto& patch : _patches)
		{
			if(!patch->source.expired())
				patches.insert(pluginLib::patchDB::PatchKey(*patch));
		}
		return setSelectedPatches(patches);
	}

	bool ListModel::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches)
	{
		if (_patches.empty())
		{
			deselectAll();
			return false;
		}

		juce::SparseSet<int> selection;

		int maxRow = std::numeric_limits<int>::min();
		int minRow = std::numeric_limits<int>::max();

		for(int i=0; i<static_cast<int>(getPatches().size()); ++i)
		{
			const auto key = pluginLib::patchDB::PatchKey(*getPatch(i));

			if (_patches.find(key) != _patches.end())
			{
				selection.addRange({ i, i + 1 });

				maxRow = std::max(maxRow, i);
				minRow = std::min(minRow, i);
			}
		}

		if(selection.isEmpty())
		{
			deselectAll();
			return false;
		}

		m_ignoreSelectedRowsChanged = true;
		setSelectedEntries(selection);
		m_ignoreSelectedRowsChanged = false;
		ensureVisible((minRow + maxRow) >> 1);
		return true;
	}

	void ListModel::activateSelectedPatch() const
	{
		const auto patches = getSelectedPatches();

		if(patches.size() == 1)
			m_patchManager.setSelectedPatch(*patches.begin(), m_search->handle);
	}

	void ListModel::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (!m_search)
			return;

		if (_dirty.searches.empty())
			return;

		if(_dirty.searches.find(m_search->handle) != _dirty.searches.end())
			setContent(m_search);
	}

	pluginLib::patchDB::DataSourceNodePtr ListModel::getDataSource() const
	{
		if(!m_search)
			return nullptr;

		return m_search->request.sourceNode;
	}

	void ListModel::setFilter(const std::string& _filter)
	{
		setFilter(_filter, m_hideDuplicatesByHash, m_hideDuplicatesByName);
	}

	void ListModel::setFilter(const std::string& _filter, const bool _hideDuplicatesByHash, const bool _hideDuplicatesByName)
	{
		if (m_filter == _filter && _hideDuplicatesByHash == m_hideDuplicatesByHash && m_hideDuplicatesByName == _hideDuplicatesByName)
			return;

		const auto selected = getSelectedPatches();

		m_filter = _filter;
		m_hideDuplicatesByHash = _hideDuplicatesByHash;
		m_hideDuplicatesByName = _hideDuplicatesByName;

		filterPatches();
		onModelChanged();

		setSelectedPatches(selected);

		redraw();

		getPatchManager().setListStatus(static_cast<uint32_t>(selected.size()), static_cast<uint32_t>(getPatches().size()));
	}

	void ListModel::sortPatches(Patches& _patches, pluginLib::patchDB::SourceType _sourceType)
	{
		std::sort(_patches.begin(), _patches.end(), [_sourceType](const Patch& _a, const Patch& _b)
		{
			const auto sourceType = _sourceType;

			if(sourceType == pluginLib::patchDB::SourceType::Folder)
			{
				const auto aSource = _a->source.lock();
				const auto bSource = _b->source.lock();
				if (*aSource != *bSource)
					return *aSource < *bSource;
			}
			else if (sourceType == pluginLib::patchDB::SourceType::File || sourceType == pluginLib::patchDB::SourceType::Rom || sourceType == pluginLib::patchDB::SourceType::LocalStorage)
			{
				if (_a->program != _b->program)
					return _a->program < _b->program;
			}

			return _a->getName().compare(_b->getName()) < 0;
		});
	}

	void ListModel::listBoxItemClicked(const int _row, const juce::MouseEvent& _mouseEvent)
	{
		if(!onClicked(_mouseEvent))
			ListBoxModel::listBoxItemClicked(_row, _mouseEvent);
	}

	void ListModel::backgroundClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!onClicked(_mouseEvent))
			ListBoxModel::backgroundClicked(_mouseEvent);
	}

	bool ListModel::showDeleteConfirmationMessageBox()
	{
		return 1 == juce::NativeMessageBox::showYesNoBox(juce::AlertWindow::WarningIcon, "Confirmation needed", "Delete selected patches from bank?");
	}

	pluginLib::patchDB::SourceType ListModel::getSourceType() const
	{
		if(!m_search)
			return pluginLib::patchDB::SourceType::Invalid;
		return m_search->getSourceType();
	}

	bool ListModel::canReorderPatches() const
	{
		if(!m_search)
			return false;
		if(getSourceType() != pluginLib::patchDB::SourceType::LocalStorage)
			return false;
		if(!m_search->request.tags.empty())
			return false;
		return true;
	}

	bool ListModel::hasTagFilters() const
	{
		if(!m_search)
			return false;
		return !m_search->request.tags.empty();
	}

	bool ListModel::hasFilters() const
	{
		return hasTagFilters() || !m_filter.empty();
	}

	pluginLib::patchDB::SearchHandle ListModel::getSearchHandle() const
	{
		if(!m_search)
			return pluginLib::patchDB::g_invalidSearchHandle;
		return m_search->handle;
	}

	void ListModel::sortPatches()
	{
		// Note: If this list is no longer sorted by calling this function, be sure to modify the second caller in state.cpp, too, as it is used to track the selected entry across multiple parts
		sortPatches(m_patches);
	}

	void ListModel::sortPatches(Patches& _patches) const
	{
		sortPatches(_patches, getSourceType());
	}

	void ListModel::filterPatches()
	{
		if (m_filter.empty() && !m_hideDuplicatesByHash && !m_hideDuplicatesByName)
		{
			m_filteredPatches.clear();
			return;
		}

		m_filteredPatches.reserve(m_patches.size());
		m_filteredPatches.clear();

		std::set<pluginLib::patchDB::PatchHash> knownHashes;
		std::set<std::string> knownNames;

		for (const auto& patch : m_patches)
		{
			if(m_hideDuplicatesByHash)
			{
				if(knownHashes.find(patch->hash) != knownHashes.end())
					continue;
				knownHashes.insert(patch->hash);
			}

			if(m_hideDuplicatesByName)
			{
				if(knownNames.find(patch->getName()) != knownNames.end())
					continue;
				knownNames.insert(patch->getName());
			}

			if (m_filter.empty() || match(patch))
				m_filteredPatches.emplace_back(patch);
		}
	}

	bool ListModel::match(const Patch& _patch) const
	{
		const auto name = _patch->getName();
		const auto t = Search::lowercase(name);
		return t.find(m_filter) != std::string::npos;
	}
}
