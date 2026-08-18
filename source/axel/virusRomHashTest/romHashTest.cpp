// Tests for ROMFile::getHash() and the DspMemoryPatchSet allowedTargets gate it feeds.
//
// WHY THIS EXISTS
//
// m_romDataHash was declared in romfile.h, returned by getHash(), and assigned NOWHERE
// in the tree. Every ROM therefore reported the default-constructed MD5 - all zeroes -
// and the only consumer, DspMemoryPatchSet::apply()'s allowedTargets list, could not
// tell one ROM from another. The check ran, returned an answer, and every caller
// believed it had chosen. A check that cannot fail is worse than no check.
//
// Both directions are covered on purpose. "The right ROM is accepted" alone would still
// pass if the gate accepted everything, which is exactly the state being fixed, so the
// rejection arms carry the weight.
//
// Each arm names the degenerate stub that reddens it, so the suite's ability to fail is
// a property of the file rather than a claim in a commit message:
//   STUB 1 - drop the m_romDataHash assignment in romfile.cpp   (the pre-fix state)
//   STUB 2 - drop the zero-MD5 guard in dspMemoryPatch.cpp
// The two stubs must redden DISJOINT sets. If one stub reddens everything, the arms are
// not independent and the suite is measuring one thing under thirteen names.
//
// Ablation results from independently removing the hash assignment and the zero guard:
//   STUB 1  ->  7/13, 6 failed   arms 1, 2a, 2b, 3, 6a, 6b
//   STUB 2  -> 11/13, 2 failed   arms 8a, 8b
//   neither -> 13/13
// Three checks - 4, 5, 7 - and arm 9 are reddened by NEITHER stub and are labelled as
// such below rather than quietly counted as coverage.

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

	// These byte vectors are NOT valid Virus ROMs and are not meant to be: initialize()
	// rejects them and clears m_romFileData. The hash is computed before that happens, on
	// purpose, so identity survives an unparseable ROM. Testing this with a real firmware
	// image would make the suite undeliverable - the image is Access's and is not
	// redistributable - and would test nothing extra, because getHash() never looks at
	// the parse result.
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

	// ARM 1 (STUB 1 reddens). The bug in its plainest form.
	check(romA.getHash() != zero, "a loaded ROM does not report the all-zero MD5",
		"getHash() = " + romA.getHash().toString());

	// ARM 2 (STUB 1 reddens). Known-answer, against a digest computed outside this tree.
	// "not zero" alone would pass on any garbage that happened to be non-zero.
	check(romA.getHash().toString() == g_md5A, "ROM A hashes to its real MD5",
		"expected " + std::string(g_md5A) + ", got " + romA.getHash().toString());
	check(romB.getHash().toString() == g_md5B, "ROM B hashes to its real MD5",
		"expected " + std::string(g_md5B) + ", got " + romB.getHash().toString());

	// ARM 3 (STUB 1 reddens). Two ROMs of identical LENGTH and identical byte multiset,
	// differing only in order.
	check(romA.getHash() != romB.getHash(), "two different ROMs hash differently");

	// ARM 4. Determinism. Note this arm passes in the BROKEN state too - both hashes were
	// zero and zero == zero - which is why it is not evidence on its own and is listed
	// under neither stub.
	check(romA.getHash() == romA2.getHash(), "the same bytes hash the same twice");

	// ARM 5. An empty ROM keeps the all-zero digest deliberately: that is the value the
	// gate refuses, so a ROM carrying no bytes cannot authorise anything.
	check(romEmpty.getHash() == zero, "an empty/invalid ROM reports the all-zero MD5",
		"getHash() = " + romEmpty.getHash().toString());

	// ---- the gate -------------------------------------------------------------------

	// A real DSP, because the accept path writes to its memory and "the patch set was
	// selected" and "the patch actually landed" are separate properties.
	virusLib::DspSingle dsp(0x040000, true, "romHashTest");
	auto& d = dsp.getDSP();

	constexpr dsp56k::TWord g_addr = 0x100;
	constexpr dsp56k::TWord g_old  = 0x000000;
	constexpr dsp56k::TWord g_new  = 0x0abcde;

	const virusLib::DspMemoryPatchSet setForA
	{
		{ baseLib::MD5(g_md5A) },
		{ { dsp56k::MemArea_P, g_addr, g_old, g_new } }
	};

	// ARM 6 (STUB 1 reddens). The right ROM passes, and the write is visible.
	d.memWriteP(g_addr, g_old);
	const bool acceptedA = setForA.apply(d, romA.getHash());
	check(acceptedA, "the ROM the patch set names is accepted");
	check(d.memory().get(dsp56k::MemArea_P, g_addr) == g_new,
		"...and the patch actually reached DSP memory",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, g_addr)));

	// ARM 7. A different ROM is rejected, and nothing is written. This is the arm the
	// whole exercise is about: it passed in the broken state only because g_patches was
	// empty upstream, never because the gate worked.
	d.memWriteP(g_addr, g_old);
	const bool acceptedB = setForA.apply(d, romB.getHash());
	check(!acceptedB, "a ROM the patch set does not name is rejected");
	check(d.memory().get(dsp56k::MemArea_P, g_addr) == g_old,
		"...and DSP memory is untouched after a rejection",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, g_addr)));

	// ARM 8 (STUB 2 reddens, and ONLY stub 2). Fail-closed: an uncomputed hash authorises
	// nothing even when the patch set's own target list carries the same uncomputed value,
	// which is precisely what an author gets by copying getHash() out of a broken build.
	const virusLib::DspMemoryPatchSet setForZero
	{
		{ baseLib::MD5() },
		{ { dsp56k::MemArea_P, g_addr, g_old, g_new } }
	};

	d.memWriteP(g_addr, g_old);
	check(!setForZero.apply(d, zero), "an all-zero MD5 matches nothing, not even an all-zero target");
	check(d.memory().get(dsp56k::MemArea_P, g_addr) == g_old,
		"...and DSP memory is untouched",
		"P:$100 = " + std::to_string(d.memory().get(dsp56k::MemArea_P, g_addr)));

	// ARM 9. The other position: a real ROM must not be let through by an all-zero entry
	// sitting in allowedTargets. NEITHER stub reddens this one, and saying so is the
	// point - with the hash restored, romA's digest simply does not equal the all-zero
	// entry, so the loop's `continue` is never what rejects it. The arm is load-bearing
	// only if a future change makes MD5 comparison lax, and it is kept for that reason
	// and not counted as evidence for either fix.
	d.memWriteP(g_addr, g_old);
	check(!setForZero.apply(d, romA.getHash()), "an all-zero target entry does not admit a real ROM");

	std::cout << "ROM hash tests: " << (g_tests - g_failed) << "/" << g_tests << " passed";
	if(g_failed)
		std::cout << ", " << g_failed << " FAILED";
	std::cout << std::endl;

	return g_failed ? 1 : 0;
}
