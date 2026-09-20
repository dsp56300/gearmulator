#include "88lib/rom/romloader.h"
#include "common/test_util.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>

#include "baseLib/filesystem.h"
#include "baseLib/sha1.h"

using namespace emu88Lib;

namespace
{
	std::string sha1Of(const std::string& _text)
	{
		return baseLib::SHA1(reinterpret_cast<const uint8_t*>(_text.data()), _text.size()).toString();
	}

	// The message tail is where a SHA-1 gets a padding length wrong: 55 bytes still fits the
	// terminator and the length, 56 forces a second chunk, and 64 is a whole chunk with an
	// entirely separate padding block after it.
	void testSha1()
	{
		CHECK_EQ(sha1Of(""), std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
		CHECK_EQ(sha1Of("abc"), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
		CHECK_EQ(sha1Of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
		         std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
		CHECK_EQ(sha1Of(std::string(55, 'a')), std::string("c1c8bbdc22796e28c0e15163d20899b65621d65a"));
		CHECK_EQ(sha1Of(std::string(56, 'a')), std::string("c2db330f6083854c99d4b5bfb6e8f29f201be699"));
		CHECK_EQ(sha1Of(std::string(64, 'a')), std::string("0098ba824b5c16427bd7a1122a5a442a25ec644d"));

		constexpr baseLib::SHA1 parsed("a9993e364706816aba3e25717850c26c9cd0d89d");
		CHECK_EQ(parsed.toString(), std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
		CHECK(parsed.isValid());
		CHECK(!baseLib::SHA1().isValid());
	}

	// A row carries exactly one identity, and two rows competing for the same slot of the same
	// board may not share it, or one of them could never be selected. The same digest in two
	// different slots is ordinary: one mask ROM serves several boards, and the MT-32's whole
	// PCM image is the CM-32L's lower chip.
	void testRegistryIsUnambiguous()
	{
		std::set<std::tuple<uint8_t, RomSlot, uint8_t, std::string>> seen;

		for (const auto& entry : g_romRegistry)
		{
			CHECK(entry.hash.isValid() != entry.sha1.isValid());
			// SHA-1 matching only ever looks at the file as it lies on disk.
			CHECK(!entry.sha1.isValid() || !entry.wordSwapped);

			const auto digest = entry.hash.isValid() ? entry.hash.toString() : entry.sha1.toString();
			for (uint8_t device = 0; device < static_cast<uint8_t>(RomDevice::Count); ++device)
			{
				if (!usedBy(entry, static_cast<RomDevice>(device)))
					continue;
				if (!seen.emplace(device, entry.slot, entry.index, digest).second)
					std::printf("FAIL duplicate digest for %s\n", describe(entry).c_str()), ++test::g_failures;
				++test::g_checks;
			}
		}
	}

	// A slot may offer its filename at more than one size - the MT-32's two firmware
	// generations, say - but one name must never stand for two different slots, which is the
	// mistake a renaming pass invites.
	void testFileSpecNamesAreUnambiguous()
	{
		std::map<std::string, std::tuple<RomDevice, RomSlot, uint8_t>> owner;
		std::set<std::pair<std::string, size_t>> seen;

		for (const auto& spec : g_romFileSpecs)
		{
			const std::tuple<RomDevice, RomSlot, uint8_t> slot{spec.device, spec.slot, spec.index};
			const auto [it, inserted] = owner.emplace(spec.filename, slot);
			if (!inserted && it->second != slot)
				std::printf("FAIL %s names more than one slot\n", spec.filename), ++test::g_failures;
			++test::g_checks;

			if (!seen.emplace(spec.filename, spec.size).second)
				std::printf("FAIL %s is listed twice at the same size\n", spec.filename), ++test::g_failures;
			++test::g_checks;
		}
	}

	// The chips have to tile the whole exactly, every index involved has to be a registered
	// slot at the right size, and a chip index may belong to only one composite.
	void testCompositeSlotsMatchRegistry()
	{
		const auto registered = [](const RomCompositeSlot& _composite, const uint8_t _index, const size_t _size)
		{
			return std::any_of(std::begin(g_romRegistry), std::end(g_romRegistry),
			                   [&](const RomRegistryEntry& _entry)
			                   {
				                   return usedBy(_entry, _composite.device) && _entry.slot == _composite.slot &&
				                       _entry.index == _index && _entry.size == _size;
			                   });
		};

		for (const auto& composite : g_romCompositeSlots)
		{
			CHECK_EQ(composite.wholeSize, composite.chipSize * 2);
			CHECK(registered(composite, composite.wholeIndex, composite.wholeSize));
			CHECK(findCompositeWhole(composite.device, composite.slot, composite.wholeIndex) == &composite);
			CHECK(!isCompositeChip(composite.device, composite.slot, composite.wholeIndex));

			for (uint8_t i = 0; i < 2; ++i)
			{
				const uint8_t index = composite.firstChip + i;
				CHECK(registered(composite, index, composite.chipSize));
				CHECK(findCompositeChip(composite.device, composite.slot, index) == &composite);
				CHECK(!findCompositeWhole(composite.device, composite.slot, index));
			}

			// A chip filename has to name a chip. A whole index is free to have other sizes
			// too: the MT-32's banked 2.x firmware fills the same slot without being made of
			// those two EPROMs.
			for (const auto& spec : g_romFileSpecs)
			{
				if (spec.device != composite.device || spec.slot != composite.slot ||
				    !isCompositeChip(spec.device, spec.slot, spec.index))
					continue;
				if (findCompositeChip(spec.device, spec.slot, spec.index) == &composite)
					CHECK_EQ(spec.size, composite.chipSize);
			}
		}
	}

	// The SC-88Pro's five chips cut the 20 MiB wave set at 4 MiB. Only the four that are
	// halves of a larger part need composites; CS4 is R01233667 itself, so wave C has none
	// and the set is still covered end to end.
	void testSc88ProChipsTileTheWaveSet()
	{
		size_t covered = 0;
		for (const auto& composite : g_romCompositeSlots)
		{
			if (composite.device != RomDevice::Sc88Pro || composite.slot != RomSlot::Wave)
				continue;
			CHECK_EQ(composite.chipSize, size_t(0x400000));
			CHECK(!composite.interleaved);
			covered += composite.wholeSize;
		}
		CHECK_EQ(covered + Sc88ProRomSet::WaveCSize, Sc88ProRomSet::WaveSize);

		for (uint8_t index = 3; index <= 6; ++index)
			CHECK(isCompositeChip(RomDevice::Sc88Pro, RomSlot::Wave, index));
		// CS4 has no chip index: nothing joins it, so it is only ever wave C.
		CHECK(!isCompositeChip(RomDevice::Sc88Pro, RomSlot::Wave, 7));
		CHECK(!findCompositeWhole(RomDevice::Sc88Pro, RomSlot::Wave, 2));
		// VE-GS Pro also accepts the equivalent five-chip layout.
		CHECK(isCompositeChip(RomDevice::VeGsPro, RomSlot::Wave, 3));
	}

	const RomRegistryEntry* rowFor(const RomDevice _device, const RomSlot _slot, const uint8_t _index,
	                               const size_t _size)
	{
		for (const auto& entry : g_romRegistry)
		{
			if (usedBy(entry, _device) && entry.slot == _slot && entry.index == _index && entry.size == _size)
				return &entry;
		}
		return nullptr;
	}

	std::string g_tempDir;

	// An inventory does not need the scanner: it is a list of files and the rows they were
	// recognized as, which is all the composite join and split work from.
	bool place(RomInventory& _inventory, const RomDevice _device, const RomSlot _slot, const uint8_t _index,
	           const std::vector<uint8_t>& _data, const std::string& _name)
	{
		const auto* entry = rowFor(_device, _slot, _index, _data.size());
		if (!entry)
			return false;
		const auto path = g_tempDir + _name;
		if (!baseLib::filesystem::writeFile(path, _data))
			return false;
		FoundRom rom;
		rom.entry = entry;
		rom.path = path;
		_inventory.add(std::move(rom));
		return true;
	}

	std::vector<uint8_t> pattern(const size_t _size, const uint8_t _seed)
	{
		std::vector<uint8_t> data(_size);
		for (size_t i = 0; i < _size; ++i)
			data[i] = static_cast<uint8_t>(i * 7 + _seed);
		return data;
	}

	// The MT-32's firmware pair sits on a 16-bit bus, so its two chips multiplex rather than
	// concatenate. Getting that backwards produces a 64 KiB image that is the right size and
	// complete nonsense, which is exactly the mistake worth a test.
	void testInterleavedComposite()
	{
		constexpr size_t half = 0x8000;
		const auto a = pattern(half, 0x11);
		const auto b = pattern(half, 0x22);

		RomInventory halves;
		CHECK(place(halves, RomDevice::Mt32, RomSlot::Control, 1, a, "mt32_a.bin"));
		CHECK(place(halves, RomDevice::Mt32, RomSlot::Control, 2, b, "mt32_b.bin"));

		std::vector<uint8_t> whole;
		CHECK(halves.has(RomDevice::Mt32, RomSlot::Control, 0));
		CHECK(halves.read(whole, RomDevice::Mt32, RomSlot::Control, 0));
		CHECK_EQ(whole.size(), half * 2);
		bool interleaved = whole.size() == half * 2;
		for (size_t i = 0; interleaved && i < half; ++i)
			interleaved = whole[i * 2] == a[i] && whole[i * 2 + 1] == b[i];
		CHECK(interleaved);

		// And back the other way, from a combined image alone.
		RomInventory combined;
		CHECK(place(combined, RomDevice::Mt32, RomSlot::Control, 0, whole, "mt32_control.bin"));
		std::vector<uint8_t> chip;
		CHECK(combined.has(RomDevice::Mt32, RomSlot::Control, 1));
		CHECK(combined.read(chip, RomDevice::Mt32, RomSlot::Control, 1));
		CHECK(chip == a);
		CHECK(combined.read(chip, RomDevice::Mt32, RomSlot::Control, 2));
		CHECK(chip == b);
	}

	// The PCM pairs are consecutive address ranges instead.
	void testConcatenatedComposite()
	{
		constexpr size_t half = 0x80000;
		const auto low = pattern(half, 0x33);
		const auto high = pattern(half, 0x44);

		RomInventory halves;
		CHECK(place(halves, RomDevice::Cm32l, RomSlot::Wave, 1, low, "cm32l_low.bin"));
		CHECK(place(halves, RomDevice::Cm32l, RomSlot::Wave, 2, high, "cm32l_high.bin"));

		std::vector<uint8_t> whole;
		CHECK(halves.read(whole, RomDevice::Cm32l, RomSlot::Wave, 0));
		CHECK_EQ(whole.size(), half * 2);
		CHECK(std::equal(low.begin(), low.end(), whole.begin()));
		CHECK(std::equal(high.begin(), high.end(), whole.begin() + half));

		RomInventory combined;
		CHECK(place(combined, RomDevice::Cm32l, RomSlot::Wave, 0, whole, "cm32l_wave.bin"));
		std::vector<uint8_t> chip;
		CHECK(combined.read(chip, RomDevice::Cm32l, RomSlot::Wave, 1));
		CHECK(chip == low);
		CHECK(combined.read(chip, RomDevice::Cm32l, RomSlot::Wave, 2));
		CHECK(chip == high);
	}

	// The SC-88Pro's chips in practice: two of them supply wave A, and C is a one-chip
	// composite whose degenerate join must still produce the whole image.
	void testSc88ProChipComposite()
	{
		constexpr size_t chipSize = 0x400000;
		const auto cs0 = pattern(chipSize, 0x61);
		const auto cs1 = pattern(chipSize, 0x62);

		RomInventory chips;
		CHECK(place(chips, RomDevice::Sc88Pro, RomSlot::Wave, 3, cs0, "cs0.bin"));
		CHECK(place(chips, RomDevice::Sc88Pro, RomSlot::Wave, 4, cs1, "cs1.bin"));

		std::vector<uint8_t> waveA;
		CHECK(chips.has(RomDevice::Sc88Pro, RomSlot::Wave, 0));
		CHECK(chips.read(waveA, RomDevice::Sc88Pro, RomSlot::Wave, 0));
		CHECK_EQ(waveA.size(), Sc88ProRomSet::WaveASize);
		CHECK(std::equal(cs0.begin(), cs0.end(), waveA.begin()));
		CHECK(std::equal(cs1.begin(), cs1.end(), waveA.begin() + chipSize));
		// Two chips of A say nothing about B.
		CHECK(!chips.has(RomDevice::Sc88Pro, RomSlot::Wave, 1));

		// CS4 is wave C, needing no join at all.
		const auto c = pattern(chipSize, 0x63);
		RomInventory cs4;
		CHECK(place(cs4, RomDevice::Sc88Pro, RomSlot::Wave, 2, c, "cs4.bin"));
		std::vector<uint8_t> whole;
		CHECK(cs4.read(whole, RomDevice::Sc88Pro, RomSlot::Wave, 2));
		CHECK(whole == c);
	}

	void testSharedProWaves()
	{
		for (const bool named : {false, true})
		for (const bool mixed : {false, true})
		{
			RomInventory inventory;
			// Native VE-GS firmware is required even when all waves come from SC-88Pro.
			CHECK(place(inventory, RomDevice::Sc88Pro, RomSlot::Control, 0, pattern(0x100000, 1), "c.bin"));
			for (uint8_t index : {2, 3, 4, 5, 6})
			{
				if (mixed && index >= 5) continue;
				const auto data = pattern(0x400000, index);
				const auto name = "shared" + std::to_string(index) + ".bin";
				if (!named)
					CHECK(place(inventory, RomDevice::Sc88Pro, RomSlot::Wave, index, data, name));
				else
				{
					FoundRom rom;
					for (const auto& spec : g_romFileSpecs)
						if (spec.device == RomDevice::Sc88Pro && spec.slot == RomSlot::Wave && spec.index == index)
							rom.namedSpec = &spec;
					CHECK(rom.namedSpec != nullptr);
					rom.path = g_tempDir + name;
					CHECK(baseLib::filesystem::writeFile(rom.path, data));
					inventory.add(std::move(rom));
				}
			}
			if (mixed)
				CHECK(place(inventory, RomDevice::VeGsPro, RomSlot::Wave, 1, pattern(0x800000, 5), "w.bin"));
			CHECK(!inventory.isComplete(RomDevice::VeGsPro));
			CHECK(place(inventory, RomDevice::VeGsPro, RomSlot::Control, 0, pattern(0x100000, 2), "c2.bin"));
			CHECK(inventory.isComplete(RomDevice::VeGsPro));
			CHECK(inventory.missingFiles(RomDevice::VeGsPro).empty());
			std::vector<uint8_t> wave;
			CHECK(inventory.read(wave, RomDevice::VeGsPro, RomSlot::Wave, 0));
			auto expected = pattern(0x400000, 3);
			const auto high = pattern(0x400000, 4);
			expected.insert(expected.end(), high.begin(), high.end());
			CHECK(wave == expected);
			CHECK(inventory.read(wave, RomDevice::VeGsPro, RomSlot::Wave, 1));
			expected = pattern(mixed ? 0x800000 : 0x400000, 5);
			if (!mixed)
			{
				const auto cs3 = pattern(0x400000, 6);
				expected.insert(expected.end(), cs3.begin(), cs3.end());
			}
			CHECK(wave == expected);
			CHECK(inventory.read(wave, RomDevice::VeGsPro, RomSlot::Wave, 2));
			CHECK(wave == pattern(0x400000, 2));
		}
		for (uint8_t i = 2; i <= 6; ++i)
			baseLib::filesystem::remove(g_tempDir + "shared" + std::to_string(i) + ".bin");
	}

	// A board is complete with either form of a composite slot, and the chips never count as a
	// requirement of their own.
	void testCompletenessIgnoresHalves()
	{
		RomInventory inventory;
		CHECK(place(inventory, RomDevice::Mt32, RomSlot::Control, 0, pattern(0x10000, 1), "c.bin"));
		CHECK(place(inventory, RomDevice::Mt32, RomSlot::Wave, 0, pattern(0x80000, 2), "w.bin"));
		CHECK(place(inventory, RomDevice::Mt32, RomSlot::Reverb, 0, pattern(0x8000, 3), "r.bin"));
		CHECK(inventory.isComplete(RomDevice::Mt32));
		CHECK(inventory.missing(RomDevice::Mt32).empty());
		CHECK(inventory.missingFiles(RomDevice::Mt32).empty());

		RomInventory fromChips;
		CHECK(place(fromChips, RomDevice::Mt32, RomSlot::Control, 1, pattern(0x8000, 4), "ca.bin"));
		CHECK(place(fromChips, RomDevice::Mt32, RomSlot::Control, 2, pattern(0x8000, 5), "cb.bin"));
		CHECK(place(fromChips, RomDevice::Mt32, RomSlot::Wave, 1, pattern(0x40000, 6), "wa.bin"));
		CHECK(place(fromChips, RomDevice::Mt32, RomSlot::Wave, 2, pattern(0x40000, 7), "wb.bin"));
		CHECK(place(fromChips, RomDevice::Mt32, RomSlot::Reverb, 0, pattern(0x8000, 8), "r2.bin"));
		CHECK(fromChips.isComplete(RomDevice::Mt32));

		// The banked 2.x firmware is not made of 1.x EPROMs and cannot be cut into them.
		RomInventory banked;
		CHECK(place(banked, RomDevice::Mt32, RomSlot::Control, 0, pattern(0x20000, 9), "c2.bin"));
		CHECK(!banked.has(RomDevice::Mt32, RomSlot::Control, 1));
		std::vector<uint8_t> chip;
		CHECK(!banked.read(chip, RomDevice::Mt32, RomSlot::Control, 1));
	}
}

int main()
{
	g_tempDir = baseLib::filesystem::validatePath("88emu_rom_test");
	baseLib::filesystem::createDirectory(g_tempDir);

	testSha1();
	testRegistryIsUnambiguous();
	testFileSpecNamesAreUnambiguous();
	testCompositeSlotsMatchRegistry();
	testSc88ProChipsTileTheWaveSet();
	testInterleavedComposite();
	testConcatenatedComposite();
	testSc88ProChipComposite();
	testSharedProWaves();
	testCompletenessIgnoresHalves();

	for (const auto* name : {"mt32_a.bin", "mt32_b.bin", "mt32_control.bin", "cm32l_low.bin", "cm32l_high.bin",
	                         "cm32l_wave.bin", "c.bin", "w.bin", "r.bin", "ca.bin", "cb.bin", "wa.bin", "wb.bin",
	                         "r2.bin", "c2.bin", "cs0.bin", "cs1.bin", "cs4.bin"})
		baseLib::filesystem::remove(g_tempDir + name);
	// Best effort: leaving an empty directory behind is harmless where this cannot remove one.
	if (!g_tempDir.empty())
		baseLib::filesystem::remove(g_tempDir.substr(0, g_tempDir.size() - 1));

	return test::finish("rom");
}
