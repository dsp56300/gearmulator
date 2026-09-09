#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "romRegistry.h"
#include "sc88types.h"

#include "baseLib/md5.h"

namespace emu88Lib
{
	// Mask-ROM dumps of 16-bit parts sometimes store the two bytes of each CPU
	// word swapped, and both orientations of the same image circulate. Brings
	// one into CPU byte order if it is swapped, detected by scoring the H8/500
	// exception table in both orientations. Safe to call on an image that is
	// already correct, and on one that is neither.
	void normalizeH8WordOrder(std::vector<uint8_t>& _data);

	struct Sc88ProRomSet
	{
		static constexpr size_t FirmwareSize = 0x100000;
		static constexpr size_t WaveASize = 0x800000;
		static constexpr size_t WaveBSize = 0x800000;
		static constexpr size_t WaveCSize = 0x400000;
		static constexpr size_t WaveSize = WaveASize + WaveBSize + WaveCSize;

		std::vector<uint8_t> firmware;
		std::vector<uint8_t> waveA;
		std::vector<uint8_t> waveB;
		std::vector<uint8_t> waveC;
		std::string firmwareName;

		bool isValid() const
		{
			return firmware.size() == FirmwareSize && waveA.size() == WaveASize &&
			       waveB.size() == WaveBSize && waveC.size() == WaveCSize;
		}

	};

	struct Sc8850RomSet
	{
		static constexpr size_t CpuSize = 0x10000;
		static constexpr size_t ProgramSize = 0x100000;
		static constexpr size_t DataSize = 0x200000;

		std::vector<uint8_t> cpu;
		std::vector<uint8_t> program;
		std::vector<uint8_t> data;

		bool isValid() const
		{
			return cpu.size() == CpuSize && program.size() == ProgramSize && data.size() == DataSize;
		}
	};

	// SC-8820 waves can supply the exact SC-88Pro set. For SC-8850 they were
	// only a bring-up source; that model requires its native pair below.
	struct Sc8820WaveRomSet
	{
		static constexpr size_t Cs0Size = 0x1000000;
		static constexpr size_t Cs1Size = 0x0800000;

	};

	// Native SC-8850 PCM dump. IC53 and IC54 are two 128-Mbit, 16-bit mask
	// ROMs. The combined dump is stored in physical-ROM order: IC53 first,
	// followed by IC54. Both XP chips share this complete bus.
	struct Sc8850WaveRomSet
	{
		static constexpr size_t ChipSize = 0x1000000;
		static constexpr size_t Size = ChipSize * 2;

		std::vector<uint8_t> romA;
		std::vector<uint8_t> romB;

		bool isValid() const
		{
			return romA.size() == ChipSize && romB.size() == ChipSize;
		}
	};

	// SC-55mk2 (sc55mk2.h): the H8/532's 32 KiB on-chip boot ROM, the program
	// ROM and the two raw PCM mask-ROM dumps.
	struct Sc55RomSet
	{
		static constexpr size_t InternalRomSize = 0x8000;
		static constexpr size_t MaximumProgramRomSize = 0x100000;
		static constexpr size_t MaximumWaveRomSize = 0x200000;

		std::vector<uint8_t> internalRom;
		std::vector<uint8_t> programRom;
		std::vector<uint8_t> waveRom[2];

		bool isValid() const
		{
			return internalRom.size() == InternalRomSize && !programRom.empty() &&
			       programRom.size() <= MaximumProgramRomSize &&
			       waveRom[0].size() <= MaximumWaveRomSize && waveRom[1].size() <= MaximumWaveRomSize;
		}

	};

	// Firmware ROM: the SC-88's single 512 KiB H8/510 control ROM
	// ("ControlROMSC88.bin", Roland part R00xxxxx).
	//
	// Normalize word-swapped firmware into CPU byte order. Registry-loaded
	// images already carry their board identity and orientation.
	class Rom
	{
	public:
		static constexpr size_t Size = 0x80000;	// 512 KiB

		Rom() = default;
		explicit Rom(const std::string& _filename);
		Rom(std::vector<uint8_t> _data, std::string _name);
		// Identified by content: _entry is the registry row this image matched,
		// and _data is already normalized into CPU byte order.
		Rom(std::vector<uint8_t> _data, const RomRegistryEntry* _entry);

		std::vector<uint8_t> takeData() { return std::move(m_data); }
		const std::string&          getName() const { return m_name; }
		const baseLib::MD5&         getHash() const { return m_hash; }

		bool isValid() const { return m_data.size() == Size; }

		// The registry row this image was identified as, or null for a dump
		// loaded from an explicit path that matches nothing catalogued.
		const RomRegistryEntry* registryEntry() const { return m_entry; }

		// Whether the image resolved to a catalogued revision at all.

		// Which board this image is, by content. The SC-88 and SC-88VL differ
		// in one port meaning (P6DR is the LCD power line on the VL only), and
		// getting it wrong blanks the display, so this must never be inferred
		// from anything but the data.
		bool modelIsKnown() const { return m_entry != nullptr; }

		// Unrecognized dumps report Sc88, which is the safe answer — it leaves
		// the VL-only P6DR handling off.
		Model model() const;

	private:
		void normalize();
		void identify();

		std::string             m_name;
		std::vector<uint8_t>    m_data;
		baseLib::MD5            m_hash;
		const RomRegistryEntry* m_entry = nullptr;
	};

	// Wave ROM: the four 2 MiB PCM mask ROMs (Roland "PCM_IC_325..328") the XP
	// reads its sample data from. The XP addresses them as four 2 MiB banks
	// selected by bits 20..21 of its 22-bit wave address, so we keep the
	// de-scrambled result as one flat 8 MiB image and let the XP index it
	// linearly.
	//
	// Raw dumps are in physical pin order and must be de-scrambled. The SC-88
	// uses the address and data permutations in unscramble().
	class WaveRom
	{
	public:
		static constexpr size_t ChipSize  = 0x200000;				// 2 MiB per chip
		static constexpr size_t ChipCount = 4;						// IC325..IC328
		static constexpr size_t Size      = ChipSize * ChipCount;	// 8 MiB logical

		WaveRom() = default;
		// _chips[b] = raw physical-pin-order dump of bank b (2 MiB each).
		explicit WaveRom(const std::array<std::vector<uint8_t>, ChipCount>& _chips);

		// De-scrambled 8 MiB logical PCM, ready for the XP.
		std::vector<uint8_t> takeData() { return std::move(m_data); }

		bool isValid() const { return m_data.size() >= Size; }

		// SC-88 de-scramble of one 1 MiB-aligned region: address bits are
		// permuted by g_addressPermutation and data bits by g_dataPermutation.
		static void unscramble(const uint8_t* _raw, size_t _rawLen, uint8_t* _dst, size_t _dstCap);

		// Address-line and data-line permutations of the SC-88's PCM ROMs.
		// Exposed for tooling (ROM dump inspection) and tests.
		static constexpr int g_addressPermutation[20] =
		{
			0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15, 18, 19
		};
		static constexpr int g_dataPermutation[8] = { 2, 0, 4, 5, 7, 6, 3, 1 };

	private:
		std::vector<uint8_t> m_data;
	};
}
