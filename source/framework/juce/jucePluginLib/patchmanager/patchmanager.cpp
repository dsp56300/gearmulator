#include "patchmanager.h"

#include "uiInterface.h"

#include "baseLib/filesystem.h"

#include "jucePluginLib/filetype.h"
#include "jucePluginLib/types.h"

#include "dsp56kBase/logging.h"

namespace pluginLib::patchManager
{
	namespace
	{
		constexpr uint32_t g_programsPerBank = 128;
	}

	PatchManager::PatchManager(const juce::File& _dataFolder) : DB(_dataFolder), m_state(*this)
	{
		if (juce::MessageManager::getInstanceWithoutCreating())
			startTimer(200);
	}

	PatchManager::~PatchManager()
	{
		shutdown();
	}

	void PatchManager::shutdown()
	{
		// a processPending() that runs on the message thread right now finishes first, later ones do nothing
		std::scoped_lock lock(m_processPendingMutex);
		m_shutdown = true;

		// waits for program changes that call into it on other threads right now
		disconnectProgramChangeRouter();

		stopTimer();
		cancelPendingUpdate();
	}

	void PatchManager::setUi(UiInterface* _ui)
	{
		m_ui = _ui;
	}

	void PatchManager::connectProgramChangeRouter(ProgramChangeRouter& _router, ProgramChangeRouter::SendFunc _sendToDevice)
	{
		m_sendToDevice = std::move(_sendToDevice);
		m_router = &_router;
		_router.setHandler(this);
	}

	void PatchManager::disconnectProgramChangeRouter()
	{
		if (!m_router)
			return;
		m_router->setHandler(nullptr);
		m_router = nullptr;
	}

	void PatchManager::processPending()
	{
		std::scoped_lock lock(m_processPendingMutex);

		if (m_shutdown)
			return;

		uiProcess();
		processLoadedByProgramChange();

		if (m_router)
			m_router->processHeldEvents(m_sendToDevice);
	}

	void PatchManager::timerCallback()
	{
		processPending();
	}

	void PatchManager::handleAsyncUpdate()
	{
		processPending();
	}

	void PatchManager::processDirty(const patchDB::Dirty& _dirty)
	{
		if (m_ui)
			m_ui->processDirty(_dirty);

		if (!_dirty.errors.empty())
			onErrors(_dirty.errors);

		if (_dirty.dataSources)
			removeDataSourceSearches();

		if (isLoading())
			return;

		if (_dirty.dataSources)
		{
			updateProgramChangeBanks(nullptr);
			return;
		}

		const auto banks = std::atomic_load(&m_programChangeBanks);

		if (!banks)
			return;

		for (const auto& [_, bank] : *banks)
		{
			if (_dirty.searches.find(bank->search) != _dirty.searches.end())
			{
				updateProgramChangeBanks(&_dirty.searches);
				return;
			}
		}
	}

	void PatchManager::onErrors(const std::vector<std::string>& _errors) const
	{
		for (const auto& error : _errors)
			LOG("Patch Manager error: " << error);
	}

	bool PatchManager::setSelectedPatch(const patchDB::PatchPtr& _patch, const patchDB::SearchHandle _fromSearch)
	{
		return setSelectedPatch(getCurrentPart(), _patch, _fromSearch);
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
				m_ui->setSelectedPatches(std::set{patch});
		}

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const patchDB::PatchPtr& _patch, const patchDB::SearchHandle _fromSearch)
	{
		if(!activatePatch(_patch, _part))
			return false;

		m_state.setSelectedPatch(_part, patchDB::PatchKey(*_patch), _fromSearch);

		if (_part == getCurrentPart())
		{
			if (m_ui)
				m_ui->setSelectedPatch(_patch);
		}

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const patchDB::PatchPtr& _patch)
	{
		if(!isValid(_patch))
			return false;

		const auto patchDs = _patch->source.lock();

		if(!patchDs)
			return false;

		if(!setSelectedPatch(_part, patchDB::PatchKey(*_patch)))
			return false;

		return true;
	}

	bool PatchManager::setSelectedPatch(const uint32_t _part, const patchDB::PatchKey& _patch)
	{
		// we've got a patch, but we do not know its search handle, i.e. which list it is part of, find the missing information

		if(!_patch.isValid())
			return false;

		const auto searchHandle = getSearchHandle(*_patch.source, _part == getCurrentPart());

		if(searchHandle == patchDB::g_invalidSearchHandle)
			return false;

		m_state.setSelectedPatch(_part, _patch, searchHandle);

		if(getCurrentPart() == _part)
		{
			if (m_ui)
				m_ui->setSelectedPatches(std::set{ _patch });
		}

		onSelectedPatchChanged(_part, _patch);

		return true;
	}

	void PatchManager::copyPatchesToLocalStorage(const patchDB::DataSourceNodePtr& _ds, const std::vector<patchDB::PatchPtr>& _patches, int _part)
	{
		copyPatchesTo(_ds, _patches, -1, [this, _part](const std::vector<patchDB::PatchPtr>& _savedPatches)
		{
			if(_part == -1 || _savedPatches.empty())
				return;

			// on the message thread, with the next uiProcess(), which does not run any more once the patch manager is gone
			runOnUiThread([this, _part, patch = _savedPatches.front()]
			{
				setSelectedPatch(_part, patch);
			});
		});
	}

	std::string PatchManager::getTagTypeName(const patchDB::TagType _type) const
	{
		const auto it = m_tagTypeNames.find(_type);
		if(it == m_tagTypeNames.end())
		{
			return {};
		}
		return it->second;
	}

	void PatchManager::setTagTypeName(const patchDB::TagType _type, const std::string& _name)
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

	bool PatchManager::selectPatch(const uint32_t _part, const patchDB::DataSource& _ds, const uint32_t _program)
	{
		const auto searchHandle = getSearchHandle(_ds, _part == getCurrentPart());

		if(searchHandle == patchDB::g_invalidSearchHandle)
			return false;

		const auto s = getSearch(searchHandle);
		if(!s)
			return false;

		patchDB::PatchPtr p;

		{
			std::shared_lock lockResults(s->resultsMutex);
			for (const auto& patch : s->results)
			{
				if(patch->program == _program)
				{
					p = patch;
					break;
				}
			}
		}

		if(!p)
			return false;

		if(!setSelectedPatch(_part, p, s->handle))
			return false;

		if(_part == getCurrentPart())
		{
			if (m_ui)
				m_ui->setSelectedPatches(std::set{ p });
		}

		return true;
	}

	bool PatchManager::copyPart(const uint8_t _target, const uint8_t _source, uint64_t _userData/* = 0*/)
	{
		if(_target == _source)
			return false;

		const auto source = requestPatchForPart(_source, _userData);
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
		if(_part >= m_state.getPartCount() || _part >= getPartCount())
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
				m_ui->setSelectedPatches(std::set<patchDB::PatchKey>{});
		}

		return true;
	}

	std::vector<patchDB::PatchPtr> PatchManager::loadPatchesFromFiles(const std::vector<std::string>& _files)
	{
		std::vector<patchDB::PatchPtr> patches;

		for (const auto& file : _files)
		{
			patchDB::DataList results;
			if(!loadFile(results, file) || results.empty())
				continue;

			const auto defaultName = results.size() == 1 ? baseLib::filesystem::stripExtension(baseLib::filesystem::getFilenameWithoutPath(file)) : "";

			for (auto& result : results)
			{
				if(const auto patch = createPatch(std::move(result), defaultName))
					patches.push_back(patch);
			}
		}
		return patches;
	}

	void PatchManager::onLoadFinished()
	{
		DB::onLoadFinished();

		for(uint32_t i=0; i<std::min(getPartCount(), m_state.getPartCount()); ++i)
		{
			// otherwise, try to restore from the currently loaded patch
			if(!restoreSelectedPatch(i) && !m_state.isValid(i))
				updateStateAsync(i, requestPatchForPart(i));
		}

		updateProgramChangeBanks(nullptr);
	}

	bool PatchManager::restoreSelectedPatch(const uint32_t _part)
	{
		// If the state has been deserialized, the patch key is valid but the search handle is not. Only restore if that is the case
		const auto p = m_state.getPatch(_part);

		if(!p.isValid() || m_state.getSearchHandle(_part) != patchDB::g_invalidSearchHandle)
			return false;

		if(!setSelectedPatch(_part, p))
			m_state.clear(_part);

		return true;
	}

	void PatchManager::setPerInstanceConfig(const std::vector<uint8_t>& _data)
	{
		if(_data.empty())
			return;
		try
		{
			PluginStream s(_data);
			const auto version = s.read<uint32_t>();
			if(version != 1)
				return;
			m_state.setConfig(s);
		}
		catch(std::range_error& e)
		{
			LOG("Failed to to load per instance config: " << e.what());
			return;
		}

		// the patches of a state that arrives while loading are restored by onLoadFinished(), those of a later one here
		runOnUiThread([this]
		{
			if(isLoading())
				return;
			for(uint32_t i=0; i<std::min(getPartCount(), m_state.getPartCount()); ++i)
				restoreSelectedPatch(i);
		});
		notifyPending();
	}

	void PatchManager::getPerInstanceConfig(std::vector<uint8_t>& _data) const
	{
		PluginStream s;
		s.write<uint32_t>(1);	// version
		m_state.getConfig(s);
		s.toVector(_data);
	}

	void PatchManager::onProgramChanged(const uint32_t _part)
	{
		if(isLoading())
			return;
		return;
		patchDB::Data data;
		if(!requestPatchForPart(data, _part, 0))
			return;
		const auto patch = createPatch(std::move(data), {});
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

	void PatchManager::updateStateAsync(const uint32_t _part, const patchDB::PatchPtr& _patch)
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

		findDatasourceForPatch(_patch, [this, _part](const patchDB::Search& _search)
		{
			const auto handle = _search.handle;

			std::vector<patchDB::PatchPtr> results;
			results.assign(_search.results.begin(), _search.results.end());

			if(results.empty())
			{
				// the search found no match, but it must still be cancelled/removed from the DB's
				// search list, otherwise it leaks forever and every subsequent bank load / patch
				// update has to scan it in DB::updateSearches, causing CPU usage to grow unbounded
				runOnUiThread([this, handle]
				{
					cancelSearch(handle);
				});
				return;
			}

			if(results.size() > 1)
			{
				// if there are multiple results, sort them, we prefer ROM results over other results

				std::sort(results.begin(), results.end(), [](const patchDB::PatchPtr& _a, const patchDB::PatchPtr& _b)
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

			const auto key = patchDB::PatchKey(*currentPatch);

			runOnUiThread([this, _part, key, handle]
			{
				cancelSearch(handle);
				setSelectedPatch(_part, key);
			});
		});
	}

	patchDB::SearchHandle PatchManager::getSearchHandle(const patchDB::DataSource& _ds, bool _selectTreeItem)
	{
		if (m_ui)
		{
			const auto handle = m_ui->getSearchHandle(_ds, _selectTreeItem);
			if (handle != patchDB::g_invalidSearchHandle)
				return handle;
		}

		return getDataSourceSearch(_ds);
	}

	patchDB::SearchHandle PatchManager::getDataSourceSearch(const patchDB::DataSource& _ds)
	{
		// the key of a patch restored from a saved state carries a copy of its data source, search the real one
		const auto ds = getDataSource(_ds);

		if (!ds)
			return patchDB::g_invalidSearchHandle;

		// a search follows the node it was made for. A data source that was removed and added again, as the
		// Virus does with its RAM banks, is a new node that the old search never lists
		const auto it = m_dataSourceSearches.find(*ds);
		if (it != m_dataSourceSearches.end())
		{
			if (it->second.node == ds)
				return it->second.handle;
			cancelSearch(it->second.handle);
			m_dataSourceSearches.erase(it);
		}

		patchDB::SearchRequest request;
		request.sourceNode = ds;

		const auto handle = search(std::move(request));

		if (handle != patchDB::g_invalidSearchHandle)
			m_dataSourceSearches.insert({*ds, {ds, handle}});

		return handle;
	}

	void PatchManager::removeDataSourceSearches()
	{
		for (auto it = m_dataSourceSearches.begin(); it != m_dataSourceSearches.end();)
		{
			if (getDataSource(it->first) == it->second.node)
			{
				++it;
				continue;
			}

			cancelSearch(it->second.handle);
			it = m_dataSourceSearches.erase(it);
		}
	}

	patchDB::Data PatchManager::prepareProgramChangeData(const patchDB::PatchPtr& _patch) const
	{
		return applyModifications(_patch, FileType::Empty, ExportType::EmuHardware);
	}

	bool PatchManager::createProgramChangeEvents(std::vector<synthLib::SMidiEvent>&, const patchDB::PatchPtr&, const patchDB::Data&, uint32_t) const
	{
		return false;
	}

	void PatchManager::onProgramChangeLoaded(const patchDB::PatchPtr&, uint32_t)
	{
	}

	const PatchManager::ProgramChangePatch* PatchManager::findProgramChangePatch(const ProgramChangeBanks* _banks, const uint32_t _midiBankNumber, const uint32_t _program, bool& _ready)
	{
		// without a table the database is still loading, nobody knows yet which banks have a data source
		_ready = _banks != nullptr;

		if (!_banks)
			return nullptr;

		const auto it = _banks->find(_midiBankNumber);

		if (it == _banks->end())
			return nullptr;

		const auto& bank = *it->second;

		_ready = bank.ready;

		if (!bank.ready || _program >= bank.programs.size() || !bank.programs[_program].patch)
			return nullptr;

		return &bank.programs[_program];
	}

	PatchManager::Result PatchManager::onProgramChange(const uint32_t _part, const uint32_t _midiBankNumber, const uint32_t _program, std::vector<synthLib::SMidiEvent>& _events)
	{
		// holds the table while its patches are used, the message thread may replace it meanwhile
		const auto banks = std::atomic_load(&m_programChangeBanks);

		bool ready;
		const auto* p = findProgramChangePatch(banks.get(), _midiBankNumber, _program, ready);

		if (!ready)
			return Result::Deferred;

		if (!p)
		{
			// a bank with a data source consumes its program changes, even one that has no patch
			const auto hasBank = banks->find(_midiBankNumber) != banks->end();
			return hasBank ? Result::Replaced : Result::PassThrough;
		}

		if (!canLoadProgramChangeDirectly(_part) || !createProgramChangeEvents(_events, p->patch, p->data, _part))
		{
			_events.clear();
			return Result::Deferred;
		}

		{
			std::scoped_lock lock(m_loadedByProgramChangeMutex);
			m_loadedByProgramChange.emplace_back(_part, p->patch);
		}

		notifyPending();

		return Result::Replaced;
	}

	bool PatchManager::loadProgramChange(const uint32_t _part, const uint32_t _midiBankNumber, const uint32_t _program)
	{
		bool ready;
		const auto banks = std::atomic_load(&m_programChangeBanks);
		const auto* p = findProgramChangePatch(banks.get(), _midiBankNumber, _program, ready);

		if (!ready)
			return false;

		if (!p)
			return true;	// nothing to load

		const auto patch = p->patch;

		if (activatePatch(patch, _part))
			setSelectedPatch(_part, patch);

		return true;
	}

	void PatchManager::onEventsHeld()
	{
		notifyPending();
	}

	void PatchManager::notifyPending()
	{
		// without a message thread, as in a test, whoever drives the patch manager calls processPending() itself
		if (juce::MessageManager::getInstanceWithoutCreating())
			triggerAsyncUpdate();
	}

	void PatchManager::processLoadedByProgramChange()
	{
		std::vector<std::pair<uint32_t, patchDB::PatchPtr>> loaded;

		{
			std::scoped_lock lock(m_loadedByProgramChangeMutex);
			loaded.swap(m_loadedByProgramChange);
		}

		// only the last patch that a part got matters
		for (size_t i=0; i<loaded.size(); ++i)
		{
			const auto& [part, patch] = loaded[i];

			const auto superseded = std::any_of(loaded.begin() + static_cast<ptrdiff_t>(i) + 1, loaded.end(), [part = part](const auto& _later)
			{
				return _later.first == part;
			});

			if (superseded)
				continue;

			setSelectedPatch(part, patch);
			onProgramChangeLoaded(patch, part);
		}
	}

	void PatchManager::updateProgramChangeBanks(const std::set<patchDB::SearchHandle>* _dirtySearches)
	{
		std::vector<patchDB::DataSourceNodePtr> dataSources;
		getDataSources(dataSources);

		const auto previous = std::atomic_load(&m_programChangeBanks);

		auto banks = std::make_shared<ProgramChangeBanks>();

		for (const auto& ds : dataSources)
		{
			if (ds->midiBankNumber == patchDB::g_invalidMidiBankNumber || banks->find(ds->midiBankNumber) != banks->end())
				continue;

			const auto searchHandle = getDataSourceSearch(*ds);

			// a bank whose patches did not change stays as it is
			if (previous && _dirtySearches)
			{
				const auto it = previous->find(ds->midiBankNumber);

				if (it != previous->end() && it->second->ready && it->second->search == searchHandle && _dirtySearches->find(searchHandle) == _dirtySearches->end())
				{
					banks->insert({ds->midiBankNumber, it->second});
					continue;
				}
			}

			auto bank = std::make_shared<ProgramChangeBank>();
			bank->search = searchHandle;

			const auto s = getSearch(searchHandle);

			if (s && s->state == patchDB::SearchState::Completed)
			{
				std::vector<patchDB::PatchPtr> patches;

				{
					std::shared_lock lock(s->resultsMutex);
					patches.assign(s->results.begin(), s->results.end());
				}

				// the order the patches are listed in, the first one with a program number wins
				sortPatches(patches, s->getSourceType());

				bank->programs.resize(g_programsPerBank);

				for (const auto& patch : patches)
				{
					if (patch->program >= g_programsPerBank || bank->programs[patch->program].patch)
						continue;

					bank->programs[patch->program] = {patch, prepareProgramChangeData(patch)};
				}

				bank->ready = true;
			}

			banks->insert({ds->midiBankNumber, std::move(bank)});
		}

		std::atomic_store(&m_programChangeBanks, std::shared_ptr<const ProgramChangeBanks>(std::move(banks)));

		// held program changes may be loadable now
		if (m_router)
			m_router->processHeldEvents(m_sendToDevice);
	}
}
