// Tests for ROMFile::getHash() and the DspMemoryPatchSet allowedTargets gate it feeds.
// m_romDataHash had no writer, so every ROM reported the default all-zero MD5 and the
// gate could not tell one ROM from another. Rejection is checked as well as acceptance,
// since a gate that accepts everything passes an acceptance-only test. Checks that also
// pass with the bug present are marked as such below.

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

	// 0x00..0xff. Reference digest computed independently:
	//   python3 -c "import hashlib;print(hashlib.md5(bytes(range(256))).hexdigest())"
	//   e2c865db4162bed963bfaa9ef6ac18f0
	std::vector<uint8_t> dataA()
	{
		std::vector<uint8_t> d(256);
		for(size_t i=0; i<d.size(); ++i)
			d[i] = static_cast<uint8_t>(i);
		return d;
	}

	// 0xff..0x00, the same 256 bytes in the other order - so a hash that merely summed or
	// counted the input would still have to distinguish them.
	//   python3 -c "import hashlib;print(hashlib.md5(bytes(range(255,-1,-1))).hexdigest())"
	//   ec6df70f2569891eae50321a9179eb82
	std::vector<uint8_t> dataB()
	{
		std::vector<uint8_t> d(256);
		for(size_t i=0; i<d.size(); ++i)
			d[i] = static_cast<uint8_t>(255 - i);
		return d;
	}

	// Arrays, not pointers: MD5's constexpr string constructor is templated on
	// `char const(&)[33]` and a decayed `const char*` does not bind to it.
	constexpr char g_md5A[] = "e2c865db4162bed963bfaa9ef6ac18f0";
	constexpr char g_md5B[] = "ec6df70f2569891eae50321a9179eb82";

	// Not valid Virus ROMs, deliberately: initialize() rejects them and clears
	// m_romFileData, and the hash is taken before that, so identity survives an
	// unparseable ROM. A real firmware image is not redistributable and would test
	// nothing extra, since getHash() never looks at the parse result.
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

	// The bug in its plainest form. Fails if the hash assignment in romfile.cpp goes.
	check(romA.getHash() != zero, "a loaded ROM does not report the all-zero MD5",
		"getHash() = " + romA.getHash().toString());

	// Known-answer, against a digest computed outside this tree. "not zero" alone would
	// pass on any garbage that happened to be non-zero.
	check(romA.getHash().toString() == g_md5A, "ROM A hashes to its real MD5",
		"expected " + std::string(g_md5A) + ", got " + romA.getHash().toString());
	check(romB.getHash().toString() == g_md5B, "ROM B hashes to its real MD5",
		"expected " + std::string(g_md5B) + ", got " + romB.getHash().toString());

	// Two ROMs of identical length and identical byte multiset, differing only in order.
	check(romA.getHash() != romB.getHash(), "two different ROMs hash differently");

	// Determinism. This also passes with the bug present - both hashes were zero, and
	// zero == zero - so it is not evidence on its own.
	check(romA.getHash() == romA2.getHash(), "the same bytes hash the same twice");

	// An empty ROM keeps the all-zero digest deliberately: that is the value the gate
	// refuses, so a ROM carrying no bytes cannot authorise anything.
	check(romEmpty.getHash() == zero, "an empty/invalid ROM reports the all-zero MD5",
		"getHash() = " + romEmpty.getHash().toString());

	// ---- the gate -------------------------------------------------------------------

	// A real DSP, because the accept path writes to its memory and "the patch set was
	// selected" and "the patch actually landed" are separate properties.
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

	// The right ROM passes, and the write is visible.
	d.memWriteP(addr, oldVal);
	const bool acceptedA = setForA.apply(d, romA.getHash());
	check(acceptedA, "the ROM the patch set names is accepted");
	check(d.memory().get(dsp56k::MemArea_P, addr) == newVal,
		"...and the patch actually reached DSP memory",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, addr)));

	// A different ROM is rejected, and nothing is written. This passed with the bug
	// present only because g_patches is empty upstream, never because the gate worked.
	d.memWriteP(addr, oldVal);
	const bool acceptedB = setForA.apply(d, romB.getHash());
	check(!acceptedB, "a ROM the patch set does not name is rejected");
	check(d.memory().get(dsp56k::MemArea_P, addr) == oldVal,
		"...and DSP memory is untouched after a rejection",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, addr)));

	// Fail-closed: an uncomputed hash authorises nothing even when the patch set's own
	// target list carries the same uncomputed value, which is what an author gets by
	// copying getHash() out of a broken build.
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

	// The other position: a real ROM must not be let through by an all-zero entry in
	// allowedTargets. This also passes with both bugs present - romA's digest simply does
	// not equal the all-zero entry, so the `continue` is never what rejects it. Kept
	// against a future change making MD5 comparison lax, not counted as evidence here.
	d.memWriteP(addr, oldVal);
	check(!setForZero.apply(d, romA.getHash()), "an all-zero target entry does not admit a real ROM");

	std::cout << "ROM hash tests: " << (g_tests - g_failed) << "/" << g_tests << " passed";
	if(g_failed)
		std::cout << ", " << g_failed << " FAILED";
	std::cout << std::endl;

	return g_failed ? 1 : 0;
}
