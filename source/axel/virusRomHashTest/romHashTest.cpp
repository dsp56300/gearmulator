#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "virusLib/romfile.h"
#include "virusLib/dspMemoryPatch.h"
#include "virusLib/dspSingle.h"

#include "baseLib/md5.h"

#include "dsp56kEmu/dsp.h"

namespace
{
	uint32_t g_tests = 0;
	uint32_t g_failed = 0;

	void check(const bool _ok, const std::string& _name, const std::string& _detail = {})
	{
		++g_tests;
		if(_ok)
		{
			std::cout << "  ok   " << _name << std::endl;
			return;
		}
		++g_failed;
		std::cout << "FAIL: " << _name;
		if(!_detail.empty())
			std::cout << " -- " << _detail;
		std::cout << std::endl;
	}

	// 0x00..0xff, and its reverse - same length and same byte multiset
	std::vector<uint8_t> dataA()
	{
		std::vector<uint8_t> d(256);
		for(size_t i=0; i<d.size(); ++i)
			d[i] = static_cast<uint8_t>(i);
		return d;
	}

	std::vector<uint8_t> dataB()
	{
		std::vector<uint8_t> d(256);
		for(size_t i=0; i<d.size(); ++i)
			d[i] = static_cast<uint8_t>(255 - i);
		return d;
	}

	// arrays, not pointers: MD5's constexpr ctor is templated on char const(&)[33]
	constexpr char g_md5A[] = "e2c865db4162bed963bfaa9ef6ac18f0";	// md5 of 0x00..0xff, computed outside this tree
	constexpr char g_md5B[] = "ec6df70f2569891eae50321a9179eb82";	// md5 of the same bytes reversed

	// deliberately not valid ROMs: initialize() rejects them and clears m_romFileData, and the
	// hash is taken before that, so identity survives an unparseable ROM
	virusLib::ROMFile makeRom(std::vector<uint8_t> _data, const std::string& _name)
	{
		return virusLib::ROMFile(std::move(_data), _name, virusLib::DeviceModel::ABC);
	}
}

int main()
{
	std::cout << "Running ROM hash tests..." << std::endl;

	const auto romA  = makeRom(dataA(), "romA");
	const auto romA2 = makeRom(dataA(), "romA-again");
	const auto romB  = makeRom(dataB(), "romB");
	const auto romEmpty = virusLib::ROMFile::invalid();

	const baseLib::MD5 zero{};

	// ---- identity -------------------------------------------------------------------

	// the bug in its plainest form
	check(romA.getHash() != zero, "a loaded ROM does not report the all-zero MD5",
		"getHash() = " + romA.getHash().toString());

	// known-answer: "not zero" alone would pass on any garbage that happened to be non-zero
	check(romA.getHash().toString() == g_md5A, "ROM A hashes to its real MD5",
		"expected " + std::string(g_md5A) + ", got " + romA.getHash().toString());
	check(romB.getHash().toString() == g_md5B, "ROM B hashes to its real MD5",
		"expected " + std::string(g_md5B) + ", got " + romB.getHash().toString());

	// identical length and byte multiset, differing only in order
	check(romA.getHash() != romB.getHash(), "two different ROMs hash differently");

	// determinism
	check(romA.getHash() == romA2.getHash(), "the same bytes hash the same twice");

	// an empty ROM keeps the all-zero digest: the value the gate refuses
	check(romEmpty.getHash() == zero, "an empty/invalid ROM reports the all-zero MD5",
		"getHash() = " + romEmpty.getHash().toString());

	// ---- the gate -------------------------------------------------------------------

	// a real DSP: "the set was selected" and "the patch landed" are separate properties
	virusLib::DspSingle dsp(0x040000, true, "romHashTest");
	auto& d = dsp.getDSP();

	constexpr dsp56k::TWord addr   = 0x100;
	constexpr dsp56k::TWord oldVal = 0x000000;
	constexpr dsp56k::TWord newVal = 0x0abcde;

	const virusLib::DspMemoryPatchSet setForA
	{
		{ baseLib::MD5(g_md5A) },
		{ { dsp56k::MemArea_P, addr, oldVal, newVal } }
	};

	// the right ROM passes, and the write is visible
	d.memWriteP(addr, oldVal);
	const bool acceptedA = setForA.apply(d, romA.getHash());
	check(acceptedA, "the ROM the patch set names is accepted");
	check(d.memory().get(dsp56k::MemArea_P, addr) == newVal,
		"...and the patch actually reached DSP memory",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, addr)));

	// a different ROM is rejected, and nothing is written
	d.memWriteP(addr, oldVal);
	const bool acceptedB = setForA.apply(d, romB.getHash());
	check(!acceptedB, "a ROM the patch set does not name is rejected");
	check(d.memory().get(dsp56k::MemArea_P, addr) == oldVal,
		"...and DSP memory is untouched after a rejection",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, addr)));

	// fail-closed: an uncomputed hash authorises nothing, not even against an all-zero target
	const virusLib::DspMemoryPatchSet setForZero
	{
		{ baseLib::MD5() },
		{ { dsp56k::MemArea_P, addr, oldVal, newVal } }
	};

	d.memWriteP(addr, oldVal);
	check(!setForZero.apply(d, zero), "an all-zero MD5 matches nothing, not even an all-zero target");
	check(d.memory().get(dsp56k::MemArea_P, addr) == oldVal,
		"...and DSP memory is untouched",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, addr)));

	// guards against a future change making MD5 comparison lax
	d.memWriteP(addr, oldVal);
	check(!setForZero.apply(d, romA.getHash()), "an all-zero target entry does not admit a real ROM");

	std::cout << "ROM hash tests: " << (g_tests - g_failed) << "/" << g_tests << " passed";
	if(g_failed)
		std::cout << ", " << g_failed << " FAILED";
	std::cout << std::endl;

	return g_failed ? 1 : 0;
}
