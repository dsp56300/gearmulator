#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#include "state.h"

#include "baseLib/event.h"

#include "jucePluginLib/patchdb/db.h"
#include "jucePluginLib/programChangeRouter.h"

#include "juce_events/juce_events.h"

namespace pluginLib::patchManager
{
	class UiInterface;

	// The patch database plus what the plugin does with it: which patch each part has loaded, browsing through a list
	// of patches, and program changes that load a patch from a MIDI bank's data source. It does not need a user
	// interface, one that shows it registers itself with setUi().
	class PatchManager : public patchDB::DB, public ProgramChangeRouter::Handler, juce::Timer, juce::AsyncUpdater
	{
	public:
		baseLib::Event<uint32_t, patchDB::PatchKey> onSelectedPatchChanged;

		explicit PatchManager(const juce::File& _dataFolder);
		~PatchManager() override;

		// Stops what calls into the patch manager from other threads and waits for what already runs there: program
		// changes, and the timer and async updates that run processPending() on the message thread. Call it before
		// deleting a patch manager: a host may delete the plugin on another thread than the message thread (LV2 on
		// Linux does), and nothing may reach the derived class while it is destroyed
		void shutdown();

		void setUi(UiInterface* _ui);
		UiInterface* getUi() const { return m_ui; }

		// Program changes of MIDI banks that have a data source load their patch from here. _sendToDevice sends what is
		// held back behind a program change whose patch has to be loaded on the message thread.
		void connectProgramChangeRouter(ProgramChangeRouter& _router, ProgramChangeRouter::SendFunc _sendToDevice);
		void disconnectProgramChangeRouter();

		// Message thread: what the database queued for it, what program changes left to do, and the MIDI that is held
		// back behind a program change. A timer calls this every 200 ms and program changes trigger it, a test calls it
		// directly.
		virtual void processPending();

		bool setSelectedPatch(const patchDB::PatchPtr& _patch, patchDB::SearchHandle _fromSearch);
		bool setSelectedPatch(uint32_t _part, const patchDB::PatchPtr& _patch, patchDB::SearchHandle _fromSearch);

		// these remember the patch of a part and show it, they do not send it to the device
		bool setSelectedPatch(uint32_t _part, const patchDB::PatchPtr& _patch);
		bool setSelectedPatch(uint32_t _part, const patchDB::PatchKey& _patch);

		bool selectPrevPreset(uint32_t _part);
		bool selectNextPreset(uint32_t _part);

		bool selectPatch(uint32_t _part, const patchDB::DataSource& _ds, uint32_t _program);

		bool copyPart(uint8_t _target, uint8_t _source, uint64_t _userData = 0);

		const State& getState() const { return m_state; }

		void copyPatchesToLocalStorage(const patchDB::DataSourceNodePtr& _ds, const std::vector<patchDB::PatchPtr>& _patches, int _part);

		std::string getTagTypeName(patchDB::TagType _type) const;
		void setTagTypeName(patchDB::TagType _type, const std::string& _name);

		std::vector<patchDB::PatchPtr> loadPatchesFromFiles(const std::vector<std::string>& _files);

		virtual uint32_t getCurrentPart() const = 0;
		virtual uint32_t getPartCount() const { return m_state.getPartCount(); }

		virtual bool activatePatch(const patchDB::PatchPtr& _patch, uint32_t _part) = 0;
		virtual bool activatePatch(const std::string& _filename, uint32_t _part);

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data) const;

		void onProgramChanged(uint32_t _part);

		void setCurrentPart(uint32_t _part);

		// ProgramChangeRouter::Handler
		Result onProgramChange(uint32_t _part, uint32_t _midiBankNumber, uint32_t _program, std::vector<synthLib::SMidiEvent>& _events) override;
		bool loadProgramChange(uint32_t _part, uint32_t _midiBankNumber, uint32_t _program) override;
		void onEventsHeld() override;

		void processDirty(const patchDB::Dirty& _dirty) override;

	protected:
		// The data a program change loads _patch from, prepared on the message thread whenever the patches of a MIDI
		// bank change. By default the patch as it is sent to the device.
		virtual patchDB::Data prepareProgramChangeData(const patchDB::PatchPtr& _patch) const;

		// Called for a program change that loads _patch, on whatever thread received it, the audio thread for example.
		// Append to _events what the device needs to load the patch into _part, built from _data. This must not touch
		// parameters, the user interface or anything else that is not thread-safe. Return false if the patch cannot be
		// loaded that way: activatePatch() loads it on the message thread then, and the MIDI behind the program change
		// waits for it.
		virtual bool createProgramChangeEvents(std::vector<synthLib::SMidiEvent>& _events, const patchDB::PatchPtr& _patch, const patchDB::Data& _data, uint32_t _part) const;

		// Message thread, after createProgramChangeEvents() loaded _patch into _part: what activatePatch() does besides
		// sending the patch, for example requesting it back from the device
		virtual void onProgramChangeLoaded(const patchDB::PatchPtr& _patch, uint32_t _part);

		// Any thread: false if a program change for _part has to load its patch with activatePatch() on the message
		// thread, for example because locked parameters have to be sent again after it
		virtual bool canLoadProgramChangeDirectly(uint32_t _part) const { return true; }

		virtual void onErrors(const std::vector<std::string>& _errors) const;

		void onLoadFinished() override;

		void updateStateAsync(uint32_t _part, const patchDB::PatchPtr& _patch);

		patchDB::SearchHandle getSearchHandle(const patchDB::DataSource& _ds, bool _selectTreeItem);

		State& getMutableState() { return m_state; }

		void timerCallback() override;

	private:
		struct ProgramChangePatch
		{
			patchDB::PatchPtr patch;
			patchDB::Data data;
		};

		struct ProgramChangeBank
		{
			bool ready = false;		// false until the search for its patches completed
			patchDB::SearchHandle search = patchDB::g_invalidSearchHandle;
			std::vector<ProgramChangePatch> programs;	// index = program number
		};

		using ProgramChangeBanks = std::map<uint32_t, std::shared_ptr<const ProgramChangeBank>>;

		void handleAsyncUpdate() override;
		void notifyPending();

		bool selectPatch(uint32_t _part, int _offset);

		// finds the patch of a part whose key came from a saved state, returns false if the part has no such patch
		bool restoreSelectedPatch(uint32_t _part);

		// _ready is false if the patches of the bank are not known yet. Returns nullptr if the bank has no data source
		// or no patch with that program number
		static const ProgramChangePatch* findProgramChangePatch(const ProgramChangeBanks* _banks, uint32_t _midiBankNumber, uint32_t _program, bool& _ready);
		void updateProgramChangeBanks(const std::set<patchDB::SearchHandle>* _dirtySearches);
		void processLoadedByProgramChange();

		// a search that lists all patches of a data source, created on demand and kept up to date by the database
		patchDB::SearchHandle getDataSourceSearch(const patchDB::DataSource& _ds);
		void removeDataSourceSearches();

		State m_state;

		std::unordered_map<patchDB::TagType, std::string> m_tagTypeNames;

		UiInterface* m_ui = nullptr;

		struct DataSourceSearch
		{
			patchDB::DataSourceNodePtr node;
			patchDB::SearchHandle handle;
		};

		std::map<patchDB::DataSource, DataSourceSearch> m_dataSourceSearches;

		// read on whatever thread a program change arrives on, replaced as a whole on the message thread
		std::shared_ptr<const ProgramChangeBanks> m_programChangeBanks;

		ProgramChangeRouter* m_router = nullptr;
		ProgramChangeRouter::SendFunc m_sendToDevice;

		std::mutex m_loadedByProgramChangeMutex;
		std::vector<std::pair<uint32_t, patchDB::PatchPtr>> m_loadedByProgramChange;

		// held while processPending() runs, recursive in case a modal loop runs the timer inside it
		std::recursive_mutex m_processPendingMutex;
		std::atomic<bool> m_shutdown = false;
	};
}
