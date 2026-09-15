#include "jucePluginLibTests.h"

#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <thread>

#include "jucePluginLib/patchdb/db.h"
#include "jucePluginLib/patchdb/patch.h"
#include "jucePluginLib/patchdb/patchmodifications.h"

namespace
{
	using namespace pluginLib::patchDB;

	// F0 3E <machine> <device> <command> <buffer> <location> <data...> <checksum> F7, the Waldorf dump layout
	synthLib::SysexBuffer dump(const uint8_t _device, const uint8_t _location, const std::initializer_list<uint8_t> _data, const uint8_t _checksum)
	{
		synthLib::SysexBuffer d{0xf0, 0x3e, 0x10, _device, 0x10, 0x00, _location};
		d.insert(d.end(), _data);
		d.push_back(_checksum);
		d.push_back(0xf7);
		return d;
	}

	synthLib::SysexBuffer concat(synthLib::SysexBuffer _a, const synthLib::SysexBuffer& _b)
	{
		_a.insert(_a.end(), _b.begin(), _b.end());
		return _a;
	}

	PatchHash hashOf(const synthLib::SysexBuffer& _sysex)
	{
		Patch p;
		p.sysex = _sysex;
		p.setHashFromMessages(7, 2);
		return p.hash;
	}

	void testHash()
	{
		std::cout << "Testing Patch::setHashFromMessages..." << std::endl;

		const auto hash = hashOf(dump(0x00, 0x01, {0x11, 0x22, 0x33}, 0x40));
		TEST_ASSERT(hash != PatchHash{});

		// the same sound from another device id, another slot, with another checksum is the same patch
		TEST_ASSERT(hashOf(dump(0x7f, 0x05, {0x11, 0x22, 0x33}, 0x12)) == hash);

		// other sound data is not
		TEST_ASSERT(hashOf(dump(0x00, 0x01, {0x11, 0x22, 0x34}, 0x40)) != hash);

		// every message of a compound counts, not only the first
		const auto first = dump(0x00, 0x01, {0x11, 0x22, 0x33}, 0x40);
		TEST_ASSERT(hashOf(concat(first, dump(0x00, 0x02, {0x44}, 0x40))) != hashOf(concat(first, dump(0x00, 0x02, {0x45}, 0x40))));

		std::cout << "  setHashFromMessages tests passed" << std::endl;
	}

	std::shared_ptr<DataSourceNode> fileSource(const std::string& _name)
	{
		auto ds = std::make_shared<DataSourceNode>();
		ds->type = SourceType::File;
		ds->name = _name;
		return ds;
	}

	void testLegacyModificationKeys()
	{
		std::cout << "Testing modifications saved under an all-zero patch hash..." << std::endl;

		const auto source = fileSource("bank.syx");

		const auto patch = std::make_shared<Patch>();
		patch->source = source;
		patch->program = 5;
		patch->sysex = dump(0x00, 0x01, {0x11}, 0x40);
		patch->setHashFromMessages(7, 2);

		const auto legacyMods = std::make_shared<PatchModifications>();
		const auto currentMods = std::make_shared<PatchModifications>();

		PatchKey legacyKey(*patch);
		legacyKey.hash.fill(0);

		std::map<PatchKey, PatchModificationsPtr> mods;

		// what a product saved before it hashed its patches is still found, by data source and program
		mods.insert({legacyKey, legacyMods});
		auto it = DB::findModifications(mods, *patch);
		TEST_ASSERT(it != mods.end() && it->second == legacyMods);

		// an entry saved with the real hash wins over it
		mods.insert({PatchKey(*patch), currentMods});
		it = DB::findModifications(mods, *patch);
		TEST_ASSERT(it != mods.end() && it->second == currentMods);

		// a zero-hash entry for another program is another patch's
		mods.clear();
		auto otherProgram = legacyKey;
		otherProgram.program = 6;
		mods.insert({otherProgram, legacyMods});
		TEST_ASSERT(DB::findModifications(mods, *patch) == mods.end());

		// and so is one for another data source
		mods.clear();
		auto otherSource = legacyKey;
		otherSource.source = fileSource("other.syx");
		mods.insert({otherSource, legacyMods});
		TEST_ASSERT(DB::findModifications(mods, *patch) == mods.end());

		std::cout << "  legacy modification key tests passed" << std::endl;
	}

	// A DB with nothing to load, enough to scan folders. The loader thread is never started.
	class FolderScanDb final : public DB
	{
	public:
		explicit FolderScanDb(const juce::File& _settingsDir) : DB(_settingsDir) {}
		~FolderScanDb() override { stopLoaderThread(); }

		bool requestPatchForPart(Data&, uint32_t, uint64_t) override { return false; }
		bool loadRomData(DataList&, uint32_t, uint32_t) override { return false; }
		PatchPtr initializePatch(Data&&, const std::string&) override { return {}; }
		Data applyModifications(const PatchPtr&, const pluginLib::FileType&, pluginLib::ExportType) const override { return {}; }
		void processDirty(const Dirty&) const override {}
	};

	void testFolderFindsSubfolders()
	{
		std::cout << "Testing DB::loadFolder..." << std::endl;

		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("patchDbTest", "");
		const auto folder = root.getChildFile("presets");
		TEST_ASSERT(folder.getChildFile("factory").createDirectory());

		// presets often live in subfolders only, and each subfolder has to become a child folder source
		bool foundSubfolder = false;
		{
			FolderScanDb db(root.getChildFile("settings"));

			const auto source = std::make_shared<DataSourceNode>();
			source->type = SourceType::Folder;
			source->name = folder.getFullPathName().toStdString();

			db.loadFolder(source);

			for (const auto& child : source->getChildren())
			{
				const auto c = child.lock();
				if (c && c->type == SourceType::Folder && juce::File(juce::String::fromUTF8(c->name.c_str())).getFileName() == "factory")
					foundSubfolder = true;
			}
		}

		root.deleteRecursively();
		TEST_ASSERT(foundSubfolder);

		std::cout << "  loadFolder tests passed" << std::endl;
	}

	// A DB with one ROM bank that holds whatever presets it is given. It registers the bank at startup, the way a
	// product registers the banks of the ROM it has loaded.
	class RomDb final : public DB
	{
	public:
		RomDb(const juce::File& _settingsDir, DataList _presets, const uint32_t _bank = 0)
			: DB(_settingsDir), m_presets(std::move(_presets)), m_bank(_bank)
		{
			startLoaderThread();
			addDataSource(romBank(_bank));
		}

		~RomDb() override { stopLoaderThread(); }

		static DataSource romBank(const uint32_t _bank = 0)
		{
			DataSource ds;
			ds.type = SourceType::Rom;
			ds.name = std::string("ROM Bank ") + static_cast<char>('A' + _bank);
			ds.bank = _bank;
			return ds;
		}

		bool waitIdle() const
		{
			for (int i = 0; i < 500 && isScanning(); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return !isScanning();
		}

		bool requestPatchForPart(Data&, uint32_t, uint64_t) override { return false; }

		bool loadRomData(DataList& _results, const uint32_t _bank, uint32_t) override
		{
			if (_bank != m_bank)
				return false;
			_results = m_presets;
			return true;
		}

		PatchPtr initializePatch(Data&& _sysex, const std::string&) override
		{
			const auto patch = std::make_shared<Patch>();
			patch->sysex = std::move(_sysex);
			patch->name = "Preset";
			patch->setHashFromMessages(7, 2);
			return patch;
		}

		Data applyModifications(const PatchPtr&, const pluginLib::FileType&, pluginLib::ExportType) const override { return {}; }
		void processDirty(const Dirty&) const override {}

	private:
		const DataList m_presets;
		const uint32_t m_bank;
	};

	void testRomBanksAreNotTakenFromCache()
	{
		std::cout << "Testing that ROM banks come from the plugin, not from the cache..." << std::endl;

		const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("patchDbTest", "");
		const auto settings = root.getChildFile("settings");

		const auto unchanged = dump(0x00, 0x00, {0x11}, 0x40);
		const auto before = dump(0x00, 0x01, {0x22}, 0x40);
		const auto after = dump(0x00, 0x01, {0x33}, 0x40);

		// first start: the user renames a factory preset and gives the bank a MIDI bank number
		{
			RomDb db(settings, {unchanged, before});
			TEST_ASSERT(db.waitIdle());

			const auto bank = db.getDataSource(RomDb::romBank());
			TEST_ASSERT(bank && bank->patches.size() == 2);

			for (const auto& patch : bank->patches)
			{
				if (patch->program == 0)
					TEST_ASSERT(db.renamePatch(patch, "Renamed"));
			}

			TEST_ASSERT(db.setDataSourceMidiBankNumber(bank, 7));
			TEST_ASSERT(db.waitIdle());
		}

		// closing the DB wrote the cache, which now holds the bank as it was
		TEST_ASSERT(settings.getChildFile("patchmanagerdb.cache").existsAsFile());

		// next start with another ROM, whose second preset differs: another model, another version, or presets built
		// by newer code
		{
			RomDb db(settings, {unchanged, after});
			TEST_ASSERT(db.waitIdle());

			const auto bank = db.getDataSource(RomDb::romBank());
			TEST_ASSERT(bank && bank->patches.size() == 2);

			for (const auto& patch : bank->patches)
			{
				if (patch->program == 1)
					TEST_ASSERT(patch->sysex == after);				// the preset of the ROM that is loaded now
				else
					TEST_ASSERT(patch->getName() == "Renamed");		// what the user set stays with the preset that did not change
			}

			TEST_ASSERT(bank->midiBankNumber == 7);
		}

		// a start with a ROM that lacks the bank, in which the user changes something else and so saves: the json and
		// the cache have to keep the MIDI bank number of the bank that is missing
		{
			RomDb db(settings, {unchanged}, 1);
			TEST_ASSERT(db.waitIdle());

			const auto bank = db.getDataSource(RomDb::romBank(1));
			TEST_ASSERT(bank && db.setDataSourceMidiBankNumber(bank, 8));
			TEST_ASSERT(db.waitIdle());
		}

		const auto midiBankNumberOfBankA = [&]
		{
			RomDb db(settings, {unchanged, after});
			const auto bank = db.waitIdle() ? db.getDataSource(RomDb::romBank()) : nullptr;
			return bank ? bank->midiBankNumber : g_invalidMidiBankNumber;
		};

		TEST_ASSERT(settings.getChildFile("patchmanagerdb.cache").existsAsFile());
		TEST_ASSERT(midiBankNumberOfBankA() == 7);		// from the cache

		TEST_ASSERT(settings.getChildFile("patchmanagerdb.cache").deleteFile());
		TEST_ASSERT(midiBankNumberOfBankA() == 7);		// from the json

		root.deleteRecursively();

		std::cout << "  ROM bank cache tests passed" << std::endl;
	}
}

void testPatchDb()
{
	testHash();
	testLegacyModificationKeys();
	testFolderFindsSubfolders();
	testRomBanksAreNotTakenFromCache();
}
