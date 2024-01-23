#include "list.h"

#include "listitem.h"
#include "patchmanager.h"
#include "search.h"
#include "../pluginEditor.h"
#include "../../juceUiLib/uiObject.h"

#include "../../juceUiLib/uiObjectStyle.h"

namespace jucePluginEditorLib::patchManager
{
	List::List(PatchManager& _pm): m_patchManager(_pm)
	{
		getViewport()->setScrollBarsShown(true, false);
		setModel(this);
		setMultipleSelectionEnabled(true);

		if (const auto& t = _pm.getTemplate("pm_listbox"))
			t->apply(_pm.getEditor(), *this);
	}

	void List::setContent(const pluginLib::patchDB::SearchHandle& _handle)
	{
		const auto& search = m_patchManager.getSearch(_handle);

		if (!search)
			return;

		setContent(search);
	}

	void List::refreshContent()
	{
		setContent(m_search);
	}

	void List::setContent(const std::shared_ptr<pluginLib::patchDB::Search>& _search)
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

		updateContent();

		setSelectedPatches(selectedPatches);

		repaint();
	}

	bool List::exportPresets(const bool _selectedOnly, FileType _fileType) const
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

		sortPatches(patches, m_search->getSourceType());

		auto& editor = m_patchManager.getEditor();

		editor.savePreset([this, p = std::move(patches), _fileType](const juce::File& _file)
		{
			exportPresets(_file, p, _fileType);
		});

		return true;
	}

	bool List::onClicked(const juce::MouseEvent& _mouseEvent) const
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

		const auto hasSelectedPatches = !getSelectedPatches().empty();

		juce::PopupMenu menu;
		if(hasSelectedPatches)
			menu.addSubMenu("Export selected...", fileTypeMenu([this](const FileType _fileType) { exportPresets(true, _fileType); }));
		menu.addSubMenu("Export all...", fileTypeMenu([this](const FileType _fileType) { exportPresets(false, _fileType); }));
		menu.addSeparator();

		if(hasSelectedPatches)
		{
			auto selectedPatches = getSelectedPatches();

			if(!m_search->request.tags.empty())
			{
				menu.addItem("Remove selected", [this, s = std::move(selectedPatches)]
				{
					const std::vector patches(s.begin(), s.end());
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
				});
			}
			else if(m_search->getSourceType() == pluginLib::patchDB::SourceType::LocalStorage)
			{
				menu.addItem("Deleted selected", [this, s = std::move(selectedPatches)]
				{
					if(juce::NativeMessageBox::showYesNoBox(juce::AlertWindow::WarningIcon, "Confirmation needed", "Delete selected patches from bank?") == 1)
					{
						const std::vector patches(s.begin(), s.end());
						m_patchManager.removePatches(m_search->request.sourceNode, patches);
					}
				});
			}
		}
		menu.showMenuAsync({});
		return true;
	}

	int List::getNumRows()
	{
		return static_cast<int>(getPatches().size());
	}

	void List::paintListBoxItem(const int _rowNumber, juce::Graphics& _g, const int _width, const int _height, const bool _rowIsSelected)
	{
		const auto* style = dynamic_cast<genericUI::UiObjectStyle*>(&getLookAndFeel());

		if (_rowNumber >= getNumRows())
			return;	// Juce what are you up to?

		const auto& patch = getPatch(_rowNumber);

		const auto text = patch->getName();

		if(style && _rowIsSelected)
		{
			_g.setColour(style->getSelectedItemBackgroundColor());
			_g.fillRect(0, 0, _width, _height);
		}

		if (style)
		{
			if (const auto f = style->getFont())
				_g.setFont(*f);
		}

		_g.setColour(findColour(textColourId));
		_g.drawText(text, 2, 0, _width - 4, _height, style ? style->getAlign() : juce::Justification::centredLeft, true);
	}

	juce::var List::getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe)
	{
		const auto& ranges = rowsToDescribe.getRanges();

		if (ranges.isEmpty())
			return {};

		juce::Array<juce::var> indices;

		for (const auto& range : ranges)
		{
			for (int i = range.getStart(); i < range.getEnd(); ++i)
			{
				if(i >= 0 && static_cast<size_t>(i) < getPatches().size())
					indices.add(i);
			}
		}

		return indices;
	}

	juce::Component* List::refreshComponentForRow(int rowNumber, bool isRowSelected, Component* existingComponentToUpdate)
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

	void List::selectedRowsChanged(const int lastRowSelected)
	{
		ListBoxModel::selectedRowsChanged(lastRowSelected);

		const auto patches = getSelectedPatches();

		if(patches.size() == 1)
			m_patchManager.setSelectedPatch(*patches.begin(), m_search->handle);
	}

	std::set<List::Patch> List::getSelectedPatches() const
	{
		std::set<Patch> result;

		const auto selectedRows = getSelectedRows();
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

	bool List::setSelectedPatches(const std::set<Patch>& _patches)
	{
		if (_patches.empty())
			return false;

		std::set<pluginLib::patchDB::PatchKey> patches;

		for (const auto& patch : _patches)
			patches.insert(pluginLib::patchDB::PatchKey(*patch));
		return setSelectedPatches(patches);
	}

	bool List::setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches)
	{
		if (_patches.empty())
		{
			deselectAllRows();
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
			deselectAllRows();
			return false;
		}

		setSelectedRows(selection);
		scrollToEnsureRowIsOnscreen((minRow + maxRow) >> 1);
		return true;
	}

	void List::processDirty(const pluginLib::patchDB::Dirty& _dirty)
	{
		if (!m_search)
			return;

		if (_dirty.searches.empty())
			return;

		if(_dirty.searches.find(m_search->handle) != _dirty.searches.end())
			setContent(m_search);
	}

	std::vector<pluginLib::patchDB::PatchPtr> List::getPatchesFromDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails) const
	{
		const auto* arr = _dragSourceDetails.description.getArray();
		if (!arr)
			return {};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (const auto& var : *arr)
		{
			if (!var.isInt())
				continue;
			const int idx = var;
			if (const auto patch = getPatch(idx))
				patches.push_back(patch);
		}

		return patches;
	}

	void List::exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const FileType _fileType) const
	{
		FileType type = _fileType;
		const auto name = Editor::createValidFilename(type, _file);

		std::vector<pluginLib::patchDB::Data> patchData;
		for (const auto& patch : _patches)
		{
			const auto patchSysex = m_patchManager.prepareSave(patch);

			if(!patchSysex.empty())
				patchData.push_back(patchSysex);
		}

		if(!m_patchManager.getEditor().savePresets(type, name, patchData))
			juce::NativeMessageBox::showMessageBox(juce::AlertWindow::WarningIcon, "Save failed", "Failed to write data to " + _file.getFullPathName().toStdString());
	}

	void List::setFilter(const std::string& _filter)
	{
		if (m_filter == _filter)
			return;

		const auto selected = getSelectedPatches();

		m_filter = _filter;

		filterPatches();
		updateContent();

		setSelectedPatches(selected);

		repaint();
	}

	void List::sortPatches(Patches& _patches, pluginLib::patchDB::SourceType _sourceType)
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

			return _a->getName().compare(_b->name) < 0;
		});
	}

	void List::listBoxItemClicked(const int _row, const juce::MouseEvent& _mouseEvent)
	{
		if(!onClicked(_mouseEvent))
			ListBoxModel::listBoxItemClicked(_row, _mouseEvent);
	}

	void List::backgroundClicked(const juce::MouseEvent& _mouseEvent)
	{
		if(!onClicked(_mouseEvent))
			ListBoxModel::backgroundClicked(_mouseEvent);
	}

	void List::sortPatches()
	{
		// Note: If this list is no longer sorted by calling this function, be sure to modify the second caller in state.cpp, too, as it is used to track the selected entry across multiple parts
		sortPatches(m_patches);
	}

	void List::sortPatches(Patches& _patches) const
	{
		sortPatches(_patches, m_search->getSourceType());
	}

	void List::filterPatches()
	{
		if (m_filter.empty())
		{
			m_filteredPatches.clear();
			return;
		}

		m_filteredPatches.reserve(m_patches.size());

		m_filteredPatches.clear();

		for (const auto& patch : m_patches)
		{
			if (match(patch))
				m_filteredPatches.emplace_back(patch);
		}
	}

	bool List::match(const Patch& _patch) const
	{
		const auto name = _patch->name;
		const auto t = Search::lowercase(name);
		return t.find(m_filter) != std::string::npos;
	}
}
