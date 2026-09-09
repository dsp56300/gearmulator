#include "rom.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <utility>

#include "baseLib/filesystem.h"

namespace emu88Lib
{
	namespace
	{
		// Score how well a candidate image's low vectors look like the H8/500
		// exception table: each live big-endian entry is a 24-bit address into
		// an early page-0 handler (0x0000_0xxx), 0xffffffff = unused slot. A
		// byte-swapped image scores far lower. (The SC-88's table starts
		// 00 00 02 00 ff ff ff ff ...)
		int vectorScore(const uint8_t* _b, const int _nVectors)
		{
			int score = 0;
			for(int v = 0; v < _nVectors; ++v)
			{
				const uint8_t* p = _b + v * 4;
				if(p[0] == 0xff && p[1] == 0xff && p[2] == 0xff && p[3] == 0xff)
					continue;
				if(p[0] == 0x00 && p[1] <= 0x0f && p[2] <= 0x0f)
					++score;
			}
			return score;
		}
	}

	// =====================================================================
	// Rom
	// =====================================================================

	void normalizeH8WordOrder(std::vector<uint8_t>& _data)
	{
		static constexpr int kVectors = 60;	// stay within the exception table
		if(_data.size() < static_cast<size_t>(kVectors) * 4)
			return;

		std::vector<uint8_t> swapped(kVectors * 4);
		for(int i = 0; i < kVectors * 4; i += 2)
		{
			swapped[i]     = _data[i + 1];
			swapped[i + 1] = _data[i];
		}
		if(vectorScore(swapped.data(), kVectors) <= vectorScore(_data.data(), kVectors))
			return;

		for(size_t i = 0; i + 1 < _data.size(); i += 2)
			std::swap(_data[i], _data[i + 1]);
	}

	void Rom::normalize()
	{
		normalizeH8WordOrder(m_data);

		// Hashed in CPU order so the same firmware identifies identically no
		// matter which of the two dump orientations it arrived in.
		m_hash = baseLib::MD5(m_data);
	}

	// The image is in CPU order and hashed by the time this runs, so the
	// registry lookup settles which board it is. A dump that matches nothing
	// keeps its filename as its name and reports no model.
	void Rom::identify()
	{
		m_entry = findRegistryEntry(m_data.size(), m_hash);
		if(m_entry)
			m_name = describe(*m_entry);
	}

	Rom::Rom(const std::string& _filename)
	{
		std::vector<uint8_t> data;
		if(!baseLib::filesystem::readFile(data, _filename))
			return;

		if(data.size() != Size)
			return;

		m_data = std::move(data);
		m_name = baseLib::filesystem::getFilenameWithoutPath(_filename);
		normalize();
		identify();
	}

	Rom::Rom(std::vector<uint8_t> _data, std::string _name)
		: m_name(std::move(_name)), m_data(std::move(_data))
	{
		if(m_data.size() != Size)
		{
			m_data.clear();
			return;
		}
		normalize();
		identify();
	}

	Rom::Rom(std::vector<uint8_t> _data, const RomRegistryEntry* _entry)
		: m_data(std::move(_data)), m_entry(_entry)
	{
		if(!m_entry || m_data.size() != m_entry->size)
		{
			m_data.clear();
			m_entry = nullptr;
			return;
		}
		// Already normalized by the loader, so this only computes the hash.
		m_hash = baseLib::MD5(m_data);
		m_name = describe(*m_entry);
	}

	Model Rom::model() const
	{
		if(m_entry && usedBy(*m_entry, RomDevice::Sc88VL))
			return Model::Sc88VL;
		return Model::Sc88;
	}

	// =====================================================================
	// WaveRom
	// =====================================================================

	void WaveRom::unscramble(const uint8_t* _raw, const size_t _rawLen, uint8_t* _dst, const size_t _dstCap)
	{
		const size_t len = std::min(_rawLen, _dstCap);

		for(size_t i = 0; i < len; ++i)
		{
			// The permutation is defined over the low 20 address bits (1 MiB);
			// anything above that selects the block and passes through.
			size_t address = i & ~static_cast<size_t>(0xfffff);
			for(int j = 0; j < 20; ++j)
			{
				if(i & (size_t(1) << j))
					address |= size_t(1) << g_addressPermutation[j];
			}

			const uint8_t src = _raw[address];
			uint8_t out = 0;
			for(int j = 0; j < 8; ++j)
			{
				if(src & (1u << g_dataPermutation[j]))
					out |= static_cast<uint8_t>(1u << j);
			}
			_dst[i] = out;
		}
	}

	WaveRom::WaveRom(const std::array<std::vector<uint8_t>, ChipCount>& _chips)
	{
		for(size_t i = 0; i < _chips.size(); ++i)
		{
			if(_chips[i].size() != ChipSize)
				return;
		}

		m_data.resize(Size);
		for(size_t b = 0; b < ChipCount; ++b)
			unscramble(_chips[b].data(), ChipSize, m_data.data() + b * ChipSize, ChipSize);
	}
}
