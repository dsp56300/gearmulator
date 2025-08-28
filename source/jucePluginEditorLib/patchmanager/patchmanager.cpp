#include "patchmanager.h"

#include "patchmanagerui.h"

#include "../pluginEditor.h"
#include "../pluginProcessor.h"

#include "baseLib/filesystem.h"

#include "jucePluginLib/clipboard.h"
#include "jucePluginLib/filetype.h"
#include "jucePluginLib/types.h"

#include "jucePluginEditorLib/patchmanagerUiRml/patchmanagerUiRml.h"

#include "dsp56kEmu/logging.h"

#include "juceUiLib/messageBox.h"

namespace jucePluginEditorLib::patchManager
{
	PatchManager::PatchManager(Editor& _editor, Rml::Element* _rootElement, const std::initializer_list<patchManager::GroupType>& _groupTypes/* = DefaultGroupTypes*/)
	: DB(juce::File(_editor.getProcessor().getPatchManagerDataFolder(false)))
	, m_state(*this)
	, m_editor(_editor)
	{
		setTagTypeName(pluginLib::patchDB::TagType::Category, "Category");
		setTagTypeName(pluginLib::patchDB::TagType::Tag, "Tag");
		setTagTypeName(pluginLib::patchDB::TagType::Favourites, "Favourite");

		startTimer(200);

		setUi(std::make_unique<patchManagerRml::PatchManagerUiRml>(m_editor, *this, *m_editor.getRmlComponent(), _rootElement, *m_editor.getPatchManagerDataModel(), _groupTypes));
	}

	PatchManager::~PatchManager()
	{
		stopTimer();
	}

	void PatchManager::timerCallback()
	{
		const juceRmlUi::RmlInterfaces::ScopedAccess access(*m_editor.getRmlComponent());
		uiProcess();
	}

	void PatchManager::processDirty(const pluginLib::patchDB::Dirty& _dirty) const
	{
		if (m_ui)
			m_ui->processDirty(_dirty);

		if(!_dirty.errors.empty())
		{
			std::string msg = "Patch Manager encountered errors:\n\n";
			for(size_t i=0; i<_dirty.errors.size(); ++i)
			{
				msg += _dirty.errors[i];
				if(i < _dirty.errors.size() - 1)
					msg += "\n";
			}

			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Patch Manager Error", msg);
		}
	}

	bool PatchManager::setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::patchDB::SearchHandle _fromSearch)
	{
		return setSelectedPatch(getCurrentPart(), _patch, _fromSearch);
	}

	void PatchManager::setCustomSearch(const pluginLib::patchDB::SearchHandle _sh) const
	{
		if (m_ui)
			m_ui->setCustomSearch(_sh);
	}

	void PatchManager::bringToFront() const
	{
		if (m_ui)
			m_ui->bringToFront();
	}

	void PatchManager::setUi(std::unique_ptr<PatchManagerUi> _ui)
	{
		m_ui = std::move(_ui);
	}

	bool PatchManager::selectPatch(const uint32_t _part, const int _offset)
	{
		auto [patch, _] = m_state.getNeighbourPreset(_part, _offset);

		if(!patch)
			return false;

		if(!setSelectedPatch(_part, patch, m_state.getSearchHandle(_part)))
			return false;

		if(_part == getCurrentPart())
		{
			if (m_ui)
				m_ui->setSelectedPatches({patch});
		}

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch)
	{
		if(!activatePatch(_patch, _part))
			return false;

		m_state.setSelectedPatch(_part, pluginLib::patchDB::PatchKey(*_patch), _fromSearch);

		if (_part == getCurrentPart())
		{
			if (m_ui)
				m_ui->setSelectedPatch(_patch);
		}

		return true;
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
		{
			if (m_ui)
				m_ui->setSelectedPatches({ _patch });
		}

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

	uint32_t PatchManager::createSaveMenuEntries(juceRmlUi::Menu& _menu, uint32_t _part, const std::string& _name/* = "patch"*/, uint64_t _userData/* = 0*/)
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
						_menu.addEntry("Overwrite " + _name + " '" + p->getName() + "' in user bank '" + ds->name + "'", true, false, [this, p, _part, _userData]
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
				_menu.addEntry("Add " + _name + " to user bank '" + ds->name + "'", true, false, [this, ds, _part, _userData]
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
			_menu.addEntry("Create new user bank and add " + _name, true, false, [this, _part, _userData]
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
		{
			if (m_ui)
				m_ui->setSelectedPatches({ p });
		}

		return true;
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
		if (!m_ui)
			return false;
		return m_ui->createTag(groupType, _name);
	}

	void PatchManager::exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const pluginLib::FileType& _fileType) const
	{
#if SYNTHLIB_DEMO_MODE
		getEditor().showDemoRestrictionMessageBox();
#else
		pluginLib::FileType type = _fileType;
		const auto name = Editor::createValidFilename(type, _file);

		std::vector<pluginLib::patchDB::Data> patchData;
		for (const auto& patch : _patches)
		{
			const auto patchSysex = applyModifications(patch, type, pluginLib::ExportType::File);

			if(!patchSysex.empty())
				patchData.push_back(patchSysex);
		}

		if(!Editor::savePresets(type, name, patchData))
			genericUI::MessageBox::showOk(genericUI::MessageBox::Icon::Warning, "Save failed", "Failed to write data to " + _file.getFullPathName().toStdString());
#endif
	}

	bool PatchManager::exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, const pluginLib::FileType& _fileType) const
	{
		const auto patchCount = _patches.size();

		auto exportPatches = [p = std::move(_patches), this, _fileType]
		{
			auto patches = p;
			PatchManagerUi::sortPatches(patches, pluginLib::patchDB::SourceType::LocalStorage);
			m_editor.savePreset(_fileType, [this, p = std::move(patches), _fileType](const juce::File& _file)
			{
				exportPresets(_file, p, _fileType);
			});
		};

		if(patchCount > 128)
		{
			genericUI::MessageBox::showOkCancel(
				genericUI::MessageBox::Icon::Warning,
				"Patch Manager", 
				"You are trying to export more than 128 presets into a single file. Note that this dump exceeds the size of one bank and may not be compatible with your hardware",
				[this, exportPatches](const genericUI::MessageBox::Result _result)
				{
					if (_result != genericUI::MessageBox::Result::Ok)
						return;

					exportPatches();
				});
		}
		else
		{
			exportPatches();
		}

		return true;
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
		{
			if (m_ui)
				m_ui->setSelectedPatches(std::set<pluginLib::patchDB::PatchKey>{});
		}

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

			const auto defaultName = results.size() == 1 ? baseLib::filesystem::stripExtension(baseLib::filesystem::getFilenameWithoutPath(file)) : "";

			for (auto& result : results)
			{
				if(const auto patch = initializePatch(std::move(result), defaultName))
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
		const auto patch = initializePatch(std::move(data), {});
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
		if (!m_ui)
			return pluginLib::patchDB::g_invalidSearchHandle;

		auto handle = m_ui->getSearchHandle(_ds, _selectTreeItem);
		if (handle != pluginLib::patchDB::g_invalidSearchHandle)
			return handle;

		const auto search = getSearch(_ds);

		if(!search)
			return pluginLib::patchDB::g_invalidSearchHandle;

		return search->handle;
	}

	std::vector<pluginLib::patchDB::PatchPtr> PatchManager::getPatchesFromString(const std::string& _text)
	{
		auto data = pluginLib::Clipboard::getDataFromString(m_editor.getProcessor(), _text);

		if(data.sysex.empty())
			return {};

		pluginLib::patchDB::DataList results;

		if (!parseFileData(results, data.sysex))
			return {};

		std::vector<pluginLib::patchDB::PatchPtr> patches;

		for (auto& result : results)
		{
			if(const auto patch = initializePatch(std::move(result), {}))
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

	std::string PatchManager::toString(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, const pluginLib::ExportType _exportType) const
	{
		if(!_patch)
			return {};

		const auto data = applyModifications(_patch, _fileType, _exportType);

		return pluginLib::Clipboard::createJsonString(m_editor.getProcessor(), {}, {}, data);
	}
}
