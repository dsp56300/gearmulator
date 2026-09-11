#include "jucePluginLibTests.h"

#include <iostream>
#include <map>
#include <memory>

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
}

void testPatchDb()
{
	testHash();
	testLegacyModificationKeys();
}
