#include "jucePluginLibTests.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <iostream>
#include <set>
#include <thread>

#include "jucePluginLib/programChangeRouter.h"
#include "jucePluginLib/patchmanager/patchmanager.h"

// The patch manager as a product uses it, without any user interface: loading data sources, user banks, tags,
// renaming and removing, which patch each part has, and program changes that load patches.

namespace
{
	using namespace pluginLib::patchDB;
	using pluginLib::ProgramChangeRouter;

	// F0 7D <name...> F7
	synthLib::SysexBuffer patchDump(const std::string& _name)
	{
		synthLib::SysexBuffer d{0xf0, 0x7d};
		d.insert(d.end(), _name.begin(), _name.end());
		d.push_back(0xf7);
		return d;
	}

	std::string nameOf(const synthLib::SysexBuffer& _dump)
	{
		if (_dump.size() < 3)
			return {};
		return {_dump.begin() + 2, _dump.end() - 1};
	}

	synthLib::SMidiEvent programChange(const uint8_t _channel, const uint8_t _program)
	{
		return {synthLib::MidiEventSource::Host, static_cast<uint8_t>(synthLib::M_PROGRAMCHANGE | _channel), _program};
	}

	synthLib::SMidiEvent controlChange(const uint8_t _channel, const uint8_t _cc, const uint8_t _value)
	{
		return {synthLib::MidiEventSource::Host, static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | _channel), _cc, _value};
	}

	// A product with one ROM bank. It records which patches it loads, and loads them like a real one: a program change
	// that it can load directly becomes one event carrying the patch, activatePatch() sends that event itself.
	class TestPatchManager final : public pluginLib::patchManager::PatchManager
	{
	public:
		TestPatchManager(const juce::File& _dir, const std::vector<std::string>& _romPatches) : PatchManager(_dir)
		{
			for (const auto& name : _romPatches)
				m_rom.push_back(patchDump(name));

			startLoaderThread();
			addDataSource(romBank());
		}

		~TestPatchManager() override
		{
			stopLoaderThread();
		}

		static DataSource romBank()
		{
			DataSource ds;
			ds.type = SourceType::Rom;
			ds.name = "Test ROM";
			ds.bank = 0;
			return ds;
		}

		// lets the loader thread finish and runs what it left for the message thread, as the timer does in a plugin
		bool waitIdle()
		{
			for (int i = 0; i < 1000; ++i)
			{
				processPending();

				if (!isScanning() && !isLoading())
				{
					// run what the last jobs queued for the message thread
					processPending();
					processPending();
					return true;
				}

				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}
			return false;
		}

		PatchPtr findPatch(const DataSourceNodePtr& _ds, const std::string& _name) const
		{
			for (const auto& p : _ds->patches)
			{
				if (p->getName() == _name)
					return p;
			}
			return {};
		}

		bool requestPatchForPart(Data& _data, const uint32_t _part, uint64_t) override
		{
			if (onRequestPatchForPart)
				onRequestPatchForPart();

			const auto it = editBuffers.find(_part);
			if (it == editBuffers.end())
				return false;
			_data = it->second;
			return true;
		}

		bool loadRomData(DataList& _results, const uint32_t _bank, uint32_t) override
		{
			if (_bank != 0)
				return false;
			_results = m_rom;
			return true;
		}

		PatchPtr initializePatch(Data&& _sysex, const std::string&) override
		{
			if (_sysex.size() < 4 || _sysex[1] != 0x7d)
				return {};

			const auto patch = std::make_shared<Patch>();
			patch->name = nameOf(_sysex);
			patch->sysex = std::move(_sysex);
			patch->setHashFromMessages(2, 1);
			return patch;
		}

		Data applyModifications(const PatchPtr& _patch, const pluginLib::FileType&, pluginLib::ExportType) const override
		{
			return patchDump(_patch->getName());
		}

		uint32_t getCurrentPart() const override
		{
			return currentPart;
		}

		bool activatePatch(const PatchPtr& _patch, const uint32_t _part) override
		{
			if (onActivatePatch)
				onActivatePatch();

			activated.emplace_back(_patch->getName(), _part);

			if (send)
				send({synthLib::MidiEventSource::Editor});

			return true;
		}

		// what a program change loads directly, on the thread that received it
		bool createProgramChangeEvents(std::vector<synthLib::SMidiEvent>& _events, const PatchPtr&, const Data& _data, uint32_t) const override
		{
			if (onCreateProgramChangeEvents)
				onCreateProgramChangeEvents();

			if (!fastPath)
				return false;

			synthLib::SMidiEvent ev(synthLib::MidiEventSource::Editor);
			ev.sysex = _data;
			_events.push_back(ev);
			return true;
		}

		void onProgramChangeLoaded(const PatchPtr& _patch, const uint32_t _part) override
		{
			loadedByProgramChange.emplace_back(_patch->getName(), _part);
		}

		bool canLoadProgramChangeDirectly(const uint32_t _part) const override
		{
			return lockedParts.find(_part) == lockedParts.end();
		}

		uint32_t currentPart = 0;
		bool fastPath = true;
		std::set<uint32_t> lockedParts;
		std::map<uint32_t, Data> editBuffers;
		std::function<void(const synthLib::SMidiEvent&)> send;

		// called first in the virtual functions, from whichever thread calls them
		std::function<void()> onRequestPatchForPart;
		std::function<void()> onActivatePatch;
		std::function<void()> onCreateProgramChangeEvents;

		std::vector<std::pair<std::string, uint32_t>> activated;
		std::vector<std::pair<std::string, uint32_t>> loadedByProgramChange;

	private:
		DataList m_rom;
	};

	juce::File createTestDir()
	{
		return juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("patchManagerTest", "");
	}

	SearchHandle searchDataSource(TestPatchManager& _pm, const DataSourceNodePtr& _ds)
	{
		SearchRequest request;
		request.sourceNode = _ds;
		const auto handle = _pm.search(std::move(request));
		TEST_ASSERT(_pm.waitIdle());
		return handle;
	}

	std::set<std::string> searchResultNames(TestPatchManager& _pm, const SearchHandle _handle)
	{
		const auto s = _pm.getSearch(_handle);
		TEST_ASSERT(s);
		std::set<std::string> names;
		std::shared_lock lock(s->resultsMutex);
		for (const auto& p : s->results)
			names.insert(p->getName());
		return names;
	}

	void testLoading()
	{
		std::cout << "Testing data sources: ROM, file, folder..." << std::endl;

		const auto root = createTestDir();
		const auto settings = root.getChildFile("settings");

		const auto folder = root.getChildFile("presets");
		TEST_ASSERT(folder.getChildFile("sub").createDirectory());

		auto writeBank = [](const juce::File& _file, const std::vector<std::string>& _names)
		{
			synthLib::SysexBuffer data;
			for (const auto& name : _names)
			{
				const auto d = patchDump(name);
				data.insert(data.end(), d.begin(), d.end());
			}
			return _file.replaceWithData(data.data(), data.size());
		};

		TEST_ASSERT(writeBank(folder.getChildFile("bank.syx"), {"File A", "File B"}));
		TEST_ASSERT(writeBank(folder.getChildFile("sub").getChildFile("more.syx"), {"Sub A"}));

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(rom && rom->patches.size() == 3);

			const std::vector<std::string> romNames{"Bass", "Lead", "Pad"};

			for (const auto& p : rom->patches)
			{
				TEST_ASSERT(p->program < 3);
				TEST_ASSERT(p->getName() == romNames[p->program]);
			}

			// a single file
			DataSource file;
			file.type = SourceType::File;
			file.origin = DataSourceOrigin::Manual;
			file.name = folder.getChildFile("bank.syx").getFullPathName().toStdString();
			pm.addDataSource(file);
			TEST_ASSERT(pm.waitIdle());

			const auto fileDs = pm.getDataSource(file);
			TEST_ASSERT(fileDs && fileDs->patches.size() == 2);
			TEST_ASSERT(pm.findPatch(fileDs, "File A") && pm.findPatch(fileDs, "File B"));

			// a folder, including its subfolders
			DataSource f;
			f.type = SourceType::Folder;
			f.origin = DataSourceOrigin::Manual;
			f.name = folder.getFullPathName().toStdString();
			pm.addDataSource(f);
			TEST_ASSERT(pm.waitIdle());

			const auto folderDs = pm.getDataSource(f);
			TEST_ASSERT(folderDs);

			const auto names = searchResultNames(pm, searchDataSource(pm, folderDs));
			TEST_ASSERT(names == (std::set<std::string>{"File A", "File B", "Sub A"}));
		}

		// the next start has all of it again, the ROM from the product and the rest from what was saved
		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			TEST_ASSERT(pm.getDataSource(TestPatchManager::romBank())->patches.size() == 3);

			DataSource f;
			f.type = SourceType::Folder;
			f.name = folder.getFullPathName().toStdString();
			const auto folderDs = pm.getDataSource(f);
			TEST_ASSERT(folderDs);
			TEST_ASSERT(searchResultNames(pm, searchDataSource(pm, folderDs)).size() == 3);
		}

		root.deleteRecursively();

		std::cout << "  data source tests passed" << std::endl;
	}

	DataSourceNodePtr createUserBank(TestPatchManager& _pm, const std::string& _name)
	{
		DataSource ds;
		ds.type = SourceType::LocalStorage;
		ds.origin = DataSourceOrigin::Manual;
		ds.name = _name;
		ds.timestamp = std::chrono::system_clock::now();

		DataSourceNodePtr result;
		_pm.addDataSource(ds, [&result](const bool _success, const DataSourceNodePtr& _ds)
		{
			if (_success)
				result = _ds;
		});
		TEST_ASSERT(_pm.waitIdle());
		return result;
	}

	void testUserBanks()
	{
		std::cout << "Testing user banks: copy, rename, remove, persist..." << std::endl;

		const auto root = createTestDir();
		const auto settings = root.getChildFile("settings");

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			const auto bank = createUserBank(pm, "My Bank");
			TEST_ASSERT(bank);

			pm.copyPatchesTo(bank, {pm.findPatch(rom, "Bass"), pm.findPatch(rom, "Pad")});
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(bank->patches.size() == 2);

			// the copies belong to the user bank, the ROM keeps its patches
			for (const auto& p : bank->patches)
				TEST_ASSERT(p->source.lock() == bank);
			TEST_ASSERT(rom->patches.size() == 3);

			TEST_ASSERT(pm.renamePatch(pm.findPatch(bank, "Bass"), "My Bass"));
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.findPatch(bank, "My Bass"));

			pm.removePatches(bank, {pm.findPatch(bank, "Pad")});
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(bank->patches.size() == 1);

			// a renamed ROM patch keeps its name, the ROM itself does not change
			TEST_ASSERT(pm.renamePatch(pm.findPatch(rom, "Lead"), "Solo"));
			TEST_ASSERT(pm.waitIdle());
		}

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto banks = pm.getDataSourcesOfSourceType(SourceType::LocalStorage);
			TEST_ASSERT(banks.size() == 1);

			const auto bank = *banks.begin();
			TEST_ASSERT(bank->name == "My Bank");
			TEST_ASSERT(bank->patches.size() == 1);
			TEST_ASSERT(pm.findPatch(bank, "My Bass"));

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.findPatch(rom, "Solo"));

			pm.removeDataSource(*bank);
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.getDataSourcesOfSourceType(SourceType::LocalStorage).empty());
		}

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.getDataSourcesOfSourceType(SourceType::LocalStorage).empty());
		}

		root.deleteRecursively();

		std::cout << "  user bank tests passed" << std::endl;
	}

	void testTags()
	{
		std::cout << "Testing tags..." << std::endl;

		const auto root = createTestDir();
		const auto settings = root.getChildFile("settings");

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			const auto bass = pm.findPatch(rom, "Bass");
			const auto lead = pm.findPatch(rom, "Lead");

			TEST_ASSERT(pm.addTag(TagType::Category, "Low"));
			std::set<Tag> categories;
			pm.getTags(TagType::Category, categories);
			TEST_ASSERT(categories.count("Low") == 1);

			TypedTags add;
			add.add(TagType::Category, "Low");
			add.add(TagType::Favourites, "Favourite");
			TEST_ASSERT(pm.modifyTags({bass}, add));
			TEST_ASSERT(pm.waitIdle());

			TEST_ASSERT(bass->getTags(TagType::Category).containsAdded("Low"));
			TEST_ASSERT(bass->getTags(TagType::Favourites).containsAdded("Favourite"));
			TEST_ASSERT(!lead->getTags(TagType::Category).containsAdded("Low"));

			// a search by tag
			SearchRequest request;
			request.tags.add(TagType::Category, "Low");
			const auto handle = pm.search(std::move(request));
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(searchResultNames(pm, handle) == std::set<std::string>{"Bass"});

			// tagging another patch shows up in the search that is open
			TypedTags addLow;
			addLow.add(TagType::Category, "Low");
			TEST_ASSERT(pm.modifyTags({lead}, addLow));
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(searchResultNames(pm, handle) == (std::set<std::string>{"Bass", "Lead"}));

			TEST_ASSERT(pm.setTagColor(TagType::Category, "Low", 0xff00ff00));
			TEST_ASSERT(pm.getTagColor(TagType::Category, "Low") == 0xff00ff00);

			// removing a tag from a patch
			TypedTags remove;
			remove.addRemoved(TagType::Category, "Low");
			TEST_ASSERT(pm.modifyTags({lead}, remove));
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(!lead->getTags(TagType::Category).containsAdded("Low"));
			TEST_ASSERT(searchResultNames(pm, handle) == std::set<std::string>{"Bass"});

			TEST_ASSERT(pm.addTag(TagType::Tag, "Unused"));
			TEST_ASSERT(pm.removeTag(TagType::Tag, "Unused"));
			std::set<Tag> tags;
			pm.getTags(TagType::Tag, tags);
			TEST_ASSERT(tags.count("Unused") == 0);
		}

		// tags, favourites and colours are still there after a restart
		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			const auto bass = pm.findPatch(rom, "Bass");
			TEST_ASSERT(bass->getTags(TagType::Category).containsAdded("Low"));
			TEST_ASSERT(bass->getTags(TagType::Favourites).containsAdded("Favourite"));
			TEST_ASSERT(!pm.findPatch(rom, "Lead")->getTags(TagType::Category).containsAdded("Low"));
			TEST_ASSERT(pm.getTagColor(TagType::Category, "Low") == 0xff00ff00);
		}

		root.deleteRecursively();

		std::cout << "  tag tests passed" << std::endl;
	}

	void testSelection()
	{
		std::cout << "Testing the patch of each part, browsing, the saved state..." << std::endl;

		const auto root = createTestDir();
		const auto settings = root.getChildFile("settings");

		std::vector<uint8_t> state;

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			const auto handle = searchDataSource(pm, rom);

			TEST_ASSERT(pm.setSelectedPatch(1, pm.findPatch(rom, "Lead"), handle));
			TEST_ASSERT(pm.activated.back() == std::make_pair(std::string("Lead"), 1u));
			TEST_ASSERT(pm.getState().getPatch(1) == PatchKey(*pm.findPatch(rom, "Lead")));

			// browsing wraps around at the end of the list
			TEST_ASSERT(pm.selectNextPreset(1));
			TEST_ASSERT(pm.activated.back().first == "Pad");
			TEST_ASSERT(pm.selectNextPreset(1));
			TEST_ASSERT(pm.activated.back().first == "Bass");
			TEST_ASSERT(pm.selectPrevPreset(1));
			TEST_ASSERT(pm.activated.back().first == "Pad");

			// the state of a project
			pm.getPerInstanceConfig(state);
		}

		{
			// a project is loaded before the database finished loading, the patch of each part is restored once it has
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			pm.setPerInstanceConfig(state);
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.getState().isValid(1));
			TEST_ASSERT(pm.getState().getPatch(1) == PatchKey(*pm.findPatch(rom, "Pad")));

			// restoring the selection does not load anything into the device, the device state does that
			TEST_ASSERT(pm.activated.empty());

			// and browsing goes on from there
			TEST_ASSERT(pm.selectNextPreset(1));
			TEST_ASSERT(pm.activated.back().first == "Bass");
		}

		{
			// a project is loaded after the database finished loading, the host restores it later or undoes a change
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.setSelectedPatch(1, pm.findPatch(rom, "Bass"), searchDataSource(pm, rom)));
			pm.activated.clear();

			pm.setPerInstanceConfig(state);

			// what the host saves right away is what it restored
			std::vector<uint8_t> saved;
			pm.getPerInstanceConfig(saved);
			TEST_ASSERT(saved == state);

			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.getState().isValid(1));
			TEST_ASSERT(pm.getState().getPatch(1) == PatchKey(*pm.findPatch(rom, "Pad")));
			TEST_ASSERT(pm.activated.empty());

			TEST_ASSERT(pm.selectNextPreset(1));
			TEST_ASSERT(pm.activated.back().first == "Bass");
		}

		{
			// without a saved state a part takes the patch its edit buffer holds, found by content
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			pm.editBuffers[2] = patchDump("Lead");
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.getState().getPatch(2) == PatchKey(*pm.findPatch(rom, "Lead")));
		}

		root.deleteRecursively();

		std::cout << "  selection tests passed" << std::endl;
	}

	// The device side of a processor: what the router lets through goes to the device, what it replaces is sent
	// instead, what it holds arrives later through the patch manager
	struct Device
	{
		explicit Device(ProgramChangeRouter& _router) : router(_router) {}

		void send(const synthLib::SMidiEvent& _ev)
		{
			std::vector<synthLib::SMidiEvent> replacement;

			switch (router.processMidiEvent(_ev, replacement))
			{
			case ProgramChangeRouter::Result::Forward:
				received.push_back(_ev);
				break;
			case ProgramChangeRouter::Result::Held:
				break;
			case ProgramChangeRouter::Result::Consumed:
				received.insert(received.end(), replacement.begin(), replacement.end());
				break;
			}
		}

		// what the device received, a patch by its name, anything else by its status byte
		std::vector<std::string> log() const
		{
			std::vector<std::string> result;
			for (const auto& ev : received)
			{
				if (!ev.sysex.empty())
					result.push_back(nameOf(ev.sysex));
				else if (ev.source == synthLib::MidiEventSource::Editor)
					result.emplace_back("activated");
				else
					result.push_back(std::to_string(ev.a) + "/" + std::to_string(ev.b));
			}
			return result;
		}

		ProgramChangeRouter& router;
		std::vector<synthLib::SMidiEvent> received;
	};

	void connect(TestPatchManager& _pm, ProgramChangeRouter& _router, Device& _device)
	{
		_pm.connectProgramChangeRouter(_router, [&_device](const synthLib::SMidiEvent& _ev) { _device.received.push_back(_ev); });

		// what activatePatch() sends goes through the processor like everything else
		_pm.send = [&_device](const synthLib::SMidiEvent& _ev) { _device.send(_ev); };
	}

	void testProgramChanges()
	{
		std::cout << "Testing program changes that load patches..." << std::endl;

		const auto root = createTestDir();
		const auto settings = root.getChildFile("settings");

		const auto cutoff = controlChange(0, 74, 10);
		const std::string cutoffLog = std::to_string(cutoff.a) + "/74";

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			ProgramChangeRouter router;
			Device device(router);
			connect(pm, router, device);

			// until the database is loaded nobody knows which banks have a data source: a program change holds
			// everything behind it until then
			device.send(programChange(0, 1));
			device.send(cutoff);
			TEST_ASSERT(device.received.empty());

			TEST_ASSERT(pm.waitIdle());

			// the ROM has no MIDI bank yet, so the program change goes to the device after all, in its place
			TEST_ASSERT(device.log() == (std::vector<std::string>{std::to_string(0xc0) + "/1", cutoffLog}));

			// give the ROM MIDI bank 5
			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.setDataSourceMidiBankNumber(rom, 5));
			TEST_ASSERT(pm.waitIdle());

			// bank 0 has no data source, passes through
			device.received.clear();
			device.send(programChange(0, 2));
			TEST_ASSERT(device.log() == std::vector<std::string>{std::to_string(0xc0) + "/2"});

			// select bank 5: the patch replaces the program change, in the same place, so the controller after it
			// changes the new patch
			device.received.clear();
			device.send(controlChange(0, synthLib::MC_BANKSELECTMSB, 0));
			device.send(controlChange(0, synthLib::MC_BANKSELECTLSB, 5));
			device.send(programChange(0, 1));
			device.send(cutoff);
			TEST_ASSERT(device.log() == (std::vector<std::string>{std::to_string(0xb0) + "/0", std::to_string(0xb0) + "/32", "Lead", cutoffLog}));

			// the part remembers the patch once the message thread got to it, without sending it a second time
			TEST_ASSERT(pm.loadedByProgramChange.empty());
			pm.processPending();
			TEST_ASSERT(pm.loadedByProgramChange == (std::vector<std::pair<std::string, uint32_t>>{{"Lead", 0}}));
			TEST_ASSERT(pm.activated.empty());
			TEST_ASSERT(pm.getState().getPatch(0) == PatchKey(*pm.findPatch(rom, "Lead")));

			// a program without a patch does nothing
			device.received.clear();
			device.send(programChange(0, 100));
			TEST_ASSERT(device.received.empty());

			// a patch the product cannot load directly loads on the message thread, and what comes behind waits for it
			pm.fastPath = false;
			device.received.clear();
			device.send(programChange(0, 2));
			device.send(cutoff);
			device.send(controlChange(1, 7, 100));
			TEST_ASSERT(device.received.empty());

			pm.processPending();
			TEST_ASSERT(pm.activated == (std::vector<std::pair<std::string, uint32_t>>{{"Pad", 0}}));
			TEST_ASSERT(device.log() == (std::vector<std::string>{"activated", cutoffLog, std::to_string(0xb1) + "/7"}));
			TEST_ASSERT(pm.getState().getPatch(0) == PatchKey(*pm.findPatch(rom, "Pad")));

			// nothing is held any more
			device.received.clear();
			device.send(cutoff);
			TEST_ASSERT(device.log() == std::vector<std::string>{cutoffLog});

			// program changes one after another, browsing: only the last one loads
			pm.activated.clear();
			device.received.clear();
			device.send(programChange(0, 0));
			device.send(programChange(0, 1));
			device.send(controlChange(0, synthLib::MC_BANKSELECTLSB, 5));
			device.send(programChange(0, 2));
			pm.processPending();
			TEST_ASSERT(pm.activated == (std::vector<std::pair<std::string, uint32_t>>{{"Pad", 0}}));
			TEST_ASSERT(device.log() == (std::vector<std::string>{std::to_string(0xb0) + "/32", "activated"}));

			// a part with locked parameters loads on the message thread too, even if the product could load it directly
			pm.fastPath = true;
			pm.lockedParts.insert(3);
			pm.activated.clear();
			device.received.clear();
			device.send(controlChange(3, synthLib::MC_BANKSELECTLSB, 5));
			device.send(programChange(3, 0));
			TEST_ASSERT(device.log() == std::vector<std::string>{std::to_string(0xb3) + "/32"});
			pm.processPending();
			TEST_ASSERT(pm.activated == (std::vector<std::pair<std::string, uint32_t>>{{"Bass", 3}}));

			// if the message thread does not get to it, the held events go to the device without the program change
			pm.fastPath = false;
			pm.activated.clear();
			device.received.clear();
			device.send(programChange(0, 0));
			device.send(cutoff);
			std::this_thread::sleep_for(std::chrono::milliseconds(ProgramChangeRouter::HoldTimeoutMs + 50));
			router.releaseStaleHold([&device](const synthLib::SMidiEvent& _ev) { device.received.push_back(_ev); });
			TEST_ASSERT(device.log() == std::vector<std::string>{cutoffLog});
			TEST_ASSERT(pm.activated.empty());

			// and the program change loads late
			pm.processPending();
			TEST_ASSERT(pm.activated == (std::vector<std::pair<std::string, uint32_t>>{{"Bass", 0}}));

			// nothing is held behind it
			device.received.clear();
			device.send(cutoff);
			TEST_ASSERT(device.log() == std::vector<std::string>{cutoffLog});

			// removing the MIDI bank lets program changes through again
			TEST_ASSERT(pm.clearDataSourceMidiBankNumber(rom));
			TEST_ASSERT(pm.waitIdle());
			device.received.clear();
			device.send(programChange(0, 1));
			TEST_ASSERT(device.log() == std::vector<std::string>{std::to_string(0xc0) + "/1"});

			pm.disconnectProgramChangeRouter();
		}

		// the MIDI bank of a data source is still there after a restart, so is the routing, without any user interface
		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(pm.waitIdle());

			const auto rom = pm.getDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.setDataSourceMidiBankNumber(rom, 0));
			TEST_ASSERT(pm.waitIdle());
		}

		{
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			ProgramChangeRouter router;
			Device device(router);
			connect(pm, router, device);

			TEST_ASSERT(pm.waitIdle());

			device.send(programChange(0, 2));
			device.send(cutoff);
			TEST_ASSERT(device.log() == (std::vector<std::string>{"Pad", cutoffLog}));

			// refreshing a data source, as the Virus does with its RAM banks whenever it receives them, keeps its MIDI
			// bank, and its patches load as before
			pm.refreshDataSource(pm.getDataSource(TestPatchManager::romBank()));
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.getDataSource(TestPatchManager::romBank())->midiBankNumber == 0);

			device.received.clear();
			device.send(programChange(0, 1));
			device.send(cutoff);
			TEST_ASSERT(device.log() == (std::vector<std::string>{"Lead", cutoffLog}));

			// one that is removed and added again is a new data source: once it has a MIDI bank, its patches load too
			pm.removeDataSource(TestPatchManager::romBank());
			pm.addDataSource(TestPatchManager::romBank());
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.setDataSourceMidiBankNumber(pm.getDataSource(TestPatchManager::romBank()), 0));
			TEST_ASSERT(pm.waitIdle());

			device.received.clear();
			device.send(programChange(0, 2));
			device.send(cutoff);
			TEST_ASSERT(device.log() == (std::vector<std::string>{"Pad", cutoffLog}));

			pm.disconnectProgramChangeRouter();
		}

		root.deleteRecursively();

		std::cout << "  program change tests passed" << std::endl;
	}
}

namespace
{
	// _whileBlocked runs on another thread and gets _block, which it has to make the patch manager call. shutdown() on
	// a third thread, as a host that deletes the plugin, has to wait until _block returns
	bool shutdownWaitsFor(TestPatchManager& _pm, std::function<void()>& _block, const std::function<void()>& _whileBlocked)
	{
		std::promise<void> entered;
		std::promise<void> release;
		std::shared_future<void> released(release.get_future());

		_block = [&entered, released, first = true]() mutable
		{
			if (!first)
				return;
			first = false;
			entered.set_value();
			released.wait();
		};

		std::thread messageThread(_whileBlocked);
		entered.get_future().wait();

		std::atomic<bool> shutDown = false;
		std::thread hostThread([&] { _pm.shutdown(); shutDown = true; });

		std::this_thread::sleep_for(std::chrono::milliseconds(200));
		const bool waited = !shutDown;

		release.set_value();
		messageThread.join();
		hostThread.join();
		_block = {};

		return waited && shutDown;
	}

	void testShutdown()
	{
		std::cout << "Testing shutdown while the message thread is in the patch manager..." << std::endl;

		const auto root = createTestDir();
		const auto settings = root.getChildFile("settings");

		{
			// processPending() loads a held program change through the router
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			ProgramChangeRouter router;
			Device device(router);
			connect(pm, router, device);
			TEST_ASSERT(pm.waitIdle());
			TEST_ASSERT(pm.setDataSourceMidiBankNumber(pm.getDataSource(TestPatchManager::romBank()), 0));
			TEST_ASSERT(pm.waitIdle());

			pm.fastPath = false;
			device.send(programChange(0, 1));
			TEST_ASSERT(device.received.empty());

			TEST_ASSERT(shutdownWaitsFor(pm, pm.onActivatePatch, [&pm] { pm.processPending(); }));
			TEST_ASSERT(pm.activated == (std::vector<std::pair<std::string, uint32_t>>{{"Lead", 0}}));

			// afterwards nothing reaches it any more: program changes go to the device, processPending() does nothing
			device.received.clear();
			device.send(programChange(0, 2));
			TEST_ASSERT(device.log() == std::vector<std::string>{std::to_string(0xc0) + "/2"});
			pm.processPending();
			TEST_ASSERT(pm.activated.size() == 1);
		}

		{
			// processPending() finishes loading the database and asks the product for the patch of each part
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			TEST_ASSERT(shutdownWaitsFor(pm, pm.onRequestPatchForPart, [&pm] { pm.waitIdle(); }));
		}

		{
			// a program change on the audio thread, outside of processPending(), is loading a patch directly
			TestPatchManager pm(settings, {"Bass", "Lead", "Pad"});
			ProgramChangeRouter router;
			Device device(router);
			connect(pm, router, device);
			TEST_ASSERT(pm.waitIdle());

			TEST_ASSERT(shutdownWaitsFor(pm, pm.onCreateProgramChangeEvents, [&device] { device.send(programChange(0, 2)); }));
			TEST_ASSERT(device.log() == std::vector<std::string>{"Pad"});
		}

		root.deleteRecursively();

		std::cout << "  shutdown tests passed" << std::endl;
	}
}

void testPatchManager()
{
	testLoading();
	testUserBanks();
	testTags();
	testSelection();
	testProgramChanges();
	testShutdown();
}
