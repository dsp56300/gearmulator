#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <cstring>

namespace rLib::rom
{
	// Convert physical ROM pin order to logical address/data order. Bit tables define
	// each wiring permutation; merged mask/shift terms are derived at compile time.
	// Pcm16 preserves A0 and supports word-sized transforms; Pcm8 permutes it.

	template<size_t MaxTerms>
	struct MaskTerms
	{
		std::array<uint32_t, MaxTerms> mask{};
		std::array<int, MaxTerms>      shift{};
		size_t                         count = 0;

		constexpr uint32_t operator()(const uint32_t _value) const
		{
			uint32_t result = 0;
			for(size_t i = 0; i < count; ++i)
			{
				result |= shift[i] >= 0
					? (_value & mask[i]) << shift[i]
					: (_value & mask[i]) >> -shift[i];
			}
			return result;
		}

		// The same terms applied to both bytes of a 16-bit word at once. Only
		// meaningful for a data permutation, which is per byte.
		constexpr MaskTerms widenToWord() const
		{
			MaskTerms r = *this;
			for(size_t i = 0; i < r.count; ++i)
				r.mask[i] |= r.mask[i] << 8;
			return r;
		}
	};

	// Build the merged terms for one direction of a bit permutation.
	//
	// _bits is the table form the dumps are documented in: logical bit j is
	// carried by physical bit _bits[j]. _toLogical picks the direction, and
	// _passHigh keeps every bit at or above Width in place, which is what makes
	// a permutation defined over one block repeat across a multi-block image.
	template<size_t Width>
	constexpr MaskTerms<Width + 1> makeMaskTerms(const std::array<int, Width>& _bits,
		const bool _toLogical, const bool _passHigh)
	{
		MaskTerms<Width + 1> terms{};

		const auto addBit = [&terms](const size_t _from, const size_t _to)
		{
			const int shift = static_cast<int>(_to) - static_cast<int>(_from);
			size_t i = 0;
			for(; i < terms.count; ++i)
			{
				if(terms.shift[i] == shift)
					break;
			}
			if(i == terms.count)
			{
				terms.shift[i] = shift;
				++terms.count;
			}
			terms.mask[i] |= uint32_t(1) << _from;
		};

		for(size_t logical = 0; logical < Width; ++logical)
		{
			const size_t physical = static_cast<size_t>(_bits[logical]);
			if(_toLogical)
				addBit(physical, logical);
			else
				addBit(logical, physical);
		}

		if(_passHigh && Width < 32)
		{
			const uint32_t high = ~((uint32_t(1) << Width) - 1);
			size_t i = 0;
			for(; i < terms.count; ++i)
			{
				if(terms.shift[i] == 0)
					break;
			}
			if(i == terms.count)
			{
				terms.shift[i] = 0;
				++terms.count;
			}
			terms.mask[i] |= high;
		}

		return terms;
	}

	// The largest k (at most 8) for which the low k address bits permute among
	// themselves. Every Roland permutation has one - 32 bytes for the PCM parts,
	// 128 for the 19-bit one - which is what lets the inner loop below hoist the
	// high-bit address work out and index the low part from a tiny table. Zero
	// if there is none, which falls back to a full address per byte.
	template<size_t Width>
	constexpr size_t closedLowBits(const std::array<int, Width>& _bits)
	{
		size_t best = 0;
		for(size_t k = 1; k <= Width && k <= 8; ++k)
		{
			uint32_t seen = 0;
			for(size_t j = 0; j < k; ++j)
				seen |= uint32_t(1) << _bits[j];
			if(seen == (uint32_t(1) << k) - 1)
				best = k;
		}
		return best;
	}

	// ---------------------------------------------------------------------
	// De-scrambler
	// ---------------------------------------------------------------------

	// Scramble is one of the declarations at the bottom of this file: an
	// AddressBits table, a DataBits table, and nothing else.
	template<typename Scramble>
	class Descrambler
	{
	public:
		static constexpr size_t AddressWidth = Scramble::AddressBits.size();

		// The permutation repeats every this many bytes, so any whole multiple
		// of it can be de-scrambled in one call.
		static constexpr size_t BlockSize = size_t(1) << AddressWidth;

		// A0 unpermuted means a 16-bit word lands as a whole, which halves the
		// address work. Derived rather than declared so it cannot go stale.
		static constexpr bool WordCapable = Scramble::AddressBits[0] == 0;

		// logical -> physical, and back.
		static constexpr size_t physicalAddress(const size_t _logical)
		{
			return (_logical & ~(BlockSize - 1)) | kToPhysical(static_cast<uint32_t>(_logical & (BlockSize - 1)));
		}
		static constexpr size_t logicalAddress(const size_t _physical)
		{
			return (_physical & ~(BlockSize - 1)) | kToLogical(static_cast<uint32_t>(_physical & (BlockSize - 1)));
		}

		static constexpr uint8_t descrambleByte(const uint8_t _raw) { return static_cast<uint8_t>(kDataToLogical(_raw)); }
		static constexpr uint8_t scrambleByte(const uint8_t _logical) { return static_cast<uint8_t>(kDataToPhysical(_logical)); }

		// Physical pin order -> logical order. Reads _rawLen bytes at most and
		// writes _dstCap at most; a length that is not a whole number of blocks
		// is allowed, and the ragged tail reads as zero rather than off the end.
		static void descramble(const uint8_t* _raw, const size_t _rawLen, uint8_t* _dst, const size_t _dstCap)
		{
			const size_t len = _rawLen < _dstCap ? _rawLen : _dstCap;
			const size_t whole = len & ~(BlockSize - 1);

			for(size_t base = 0; base < whole; base += BlockSize)
				descrambleBlock(_raw + base, _dst + base);

			// Ragged tail: gather so the bounds check lands on the read.
			for(size_t i = whole; i < len; ++i)
			{
				const size_t physical = physicalAddress(i);
				_dst[i] = descrambleByte(physical < _rawLen ? _raw[physical] : 0);
			}
		}

		// Logical order -> physical pin order, for building card images and for
		// re-scrambling an already-decoded dump. Requires complete address blocks.
		static void scramble(const uint8_t* _logical, const size_t _len, uint8_t* _dst)
		{
			if(_len % BlockSize != 0)
				throw std::invalid_argument("ROM scrambling requires complete address blocks");

			for(size_t base = 0; base < _len; base += BlockSize)
				scrambleBlock(_logical + base, _dst + base);

		}

	private:
		static constexpr auto kToPhysical     = makeMaskTerms(Scramble::AddressBits, false, false);
		static constexpr auto kToLogical      = makeMaskTerms(Scramble::AddressBits, true, false);
		static constexpr auto kDataToLogical  = makeMaskTerms(Scramble::DataBits, true, false);
		static constexpr auto kDataToPhysical = makeMaskTerms(Scramble::DataBits, false, false);
		static constexpr auto kDataToLogicalW = kDataToLogical.widenToWord();
		static constexpr auto kDataToPhysicalW = kDataToPhysical.widenToWord();

		static constexpr size_t LowBits = closedLowBits(Scramble::AddressBits);
		static constexpr size_t LowSize = size_t(1) << LowBits;

		// The low-bit half of each address permutation, small enough to stay in
		// L1 and to turn the inner loop's address into one load and one OR.
		template<const MaskTerms<AddressWidth + 1>& Terms>
		static constexpr std::array<uint32_t, LowSize> makeLowTable()
		{
			std::array<uint32_t, LowSize> t{};
			for(size_t i = 0; i < LowSize; ++i)
				t[i] = Terms(static_cast<uint32_t>(i));
			return t;
		}
		static constexpr auto kLowToLogical  = makeLowTable<kToLogical>();
		static constexpr auto kLowToPhysical = makeLowTable<kToPhysical>();

		static bool wordAligned(const void* _a, const void* _b)
		{
			return ((reinterpret_cast<uintptr_t>(_a) | reinterpret_cast<uintptr_t>(_b)) & 1) == 0;
		}

		// Scatter: the read side walks the block in order and only the write
		// address is permuted, which is measurably friendlier than the reverse.
		// The outer loop advances one closed low-bit group at a time so the
		// high-bit address work happens once per group rather than per byte.
		static void descrambleBlock(const uint8_t* _raw, uint8_t* _dst)
		{
			if constexpr(WordCapable)
			{
				if(wordAligned(_raw, _dst))
				{
					for(size_t hi = 0; hi < BlockSize; hi += LowSize)
					{
						const size_t base = kToLogical(static_cast<uint32_t>(hi));
						for(size_t lo = 0; lo < LowSize; lo += 2)
						{
							uint16_t word;
							std::memcpy(&word, _raw + hi + lo, sizeof(word));
							word = static_cast<uint16_t>(kDataToLogicalW(word));
							std::memcpy(_dst + (base | kLowToLogical[lo]), &word, sizeof(word));
						}
					}
					return;
				}
			}

			for(size_t hi = 0; hi < BlockSize; hi += LowSize)
			{
				const size_t base = kToLogical(static_cast<uint32_t>(hi));
				for(size_t lo = 0; lo < LowSize; ++lo)
					_dst[base | kLowToLogical[lo]] = descrambleByte(_raw[hi + lo]);
			}
		}

		static void scrambleBlock(const uint8_t* _logical, uint8_t* _dst)
		{
			if constexpr(WordCapable)
			{
				if(wordAligned(_logical, _dst))
				{
					for(size_t hi = 0; hi < BlockSize; hi += LowSize)
					{
						const size_t base = kToPhysical(static_cast<uint32_t>(hi));
						for(size_t lo = 0; lo < LowSize; lo += 2)
						{
							uint16_t word;
							std::memcpy(&word, _logical + hi + lo, sizeof(word));
							word = static_cast<uint16_t>(kDataToPhysicalW(word));
							std::memcpy(_dst + (base | kLowToPhysical[lo]), &word, sizeof(word));
						}
					}
					return;
				}
			}

			for(size_t hi = 0; hi < BlockSize; hi += LowSize)
			{
				const size_t base = kToPhysical(static_cast<uint32_t>(hi));
				for(size_t lo = 0; lo < LowSize; ++lo)
					_dst[base | kLowToPhysical[lo]] = scrambleByte(_logical[hi + lo]);
			}
		}
	};

	// ---------------------------------------------------------------------
	// The permutations
	// ---------------------------------------------------------------------
	// Both tables read the same way: logical bit j is carried by physical bit
	// table[j].

	// Address order preserving adjacent byte pairs.
	struct Pcm16Scramble
	{
		static constexpr std::array<int, 20> AddressBits =
			{ 0, 4, 2, 3, 1, 13, 7, 12, 5, 10, 16, 9, 6, 8, 14, 17, 11, 15, 18, 19 };
		static constexpr std::array<int, 8> DataBits = { 2, 0, 4, 5, 7, 6, 3, 1 };
	};

	// Same data order as Pcm16; permuted A0 requires byte-sized transforms.
	struct Pcm8Scramble
	{
		static constexpr std::array<int, 20> AddressBits =
			{ 2, 0, 3, 4, 1, 9, 13, 10, 18, 17, 6, 15, 11, 16, 8, 5, 12, 7, 14, 19 };
		static constexpr std::array<int, 8> DataBits = { 2, 0, 4, 5, 7, 6, 3, 1 };
	};

	// 19-bit board wave-ROM wiring.
	struct Wave19Scramble
	{
		static constexpr std::array<int, 19> AddressBits =
			{ 0, 5, 4, 6, 1, 2, 3, 8, 10, 13, 9, 7, 11, 12, 16, 14, 15, 17, 18 };
		static constexpr std::array<int, 8> DataBits = { 6, 4, 0, 5, 3, 7, 2, 1 };
	};

	// The SN-U110 card bus: Wave19 with address lines 7..16 permuted
	// differently. Same data order.
	struct Wave19CardScramble
	{
		static constexpr std::array<int, 19> AddressBits =
			{ 0, 5, 4, 6, 1, 2, 3, 15, 13, 10, 14, 7, 12, 11, 16, 9, 8, 17, 18 };
		static constexpr std::array<int, 8> DataBits = Wave19Scramble::DataBits;
	};

	using Pcm16      = Descrambler<Pcm16Scramble>;
	using Pcm8       = Descrambler<Pcm8Scramble>;
	using Wave19     = Descrambler<Wave19Scramble>;
	using Wave19Card = Descrambler<Wave19CardScramble>;
}
