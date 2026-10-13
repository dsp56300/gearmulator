#include "88lib/mt32/names.h"

#include "88lib/boards/cm32p.h"
#include "88lib/mt32/mt32.h"
#include "88lib/rom/romloader.h"

#include "common/romDescramble.h"

namespace emu88Lib::mt32
{
	namespace
	{
		const std::string g_none;

		// A table of 16 bit pointers, little endian as the 8095 reads them, each to a timbre's common block, which
		// starts with its name. A pointer is an address in the CPU's window onto the ROM page, base makes it an offset
		// into the image.
		struct TimbreTable
		{
			uint8_t group;
			uint32_t table;
			uint32_t base;
			uint8_t count;
		};

		// The MT-32 1.x pages groups a and b in 16 KiB each, every page with its table first, and has 30 rhythm
		// timbres. The MT-32 2.x, the CM-32L and the LAPC-I keep a and b in one table of 128 and have 64 rhythm
		// timbres. The firmware versions of a generation do not move them.
		constexpr TimbreTable g_oldLayout[] = {{0, 0x8000, 0x0000, 64}, {1, 0xc000, 0x4000, 64}, {3, 0x3200, 0, 30}};
		constexpr TimbreTable g_newLayout[] = {{0, 0x8000, 0x8000, 64}, {1, 0x8080, 0x8000, 64}, {3, 0x8500, 0, 64}};

		// A timbre's common block as the firmware takes it: a printable name, structures 1-13 and a partial mute
		// and an envelope mode in range
		bool isTimbre(const std::vector<uint8_t>& _rom, const size_t _offset)
		{
			if(_offset + g_timbreCommonSize > _rom.size())
				return false;
			for(size_t i = 0; i < 10; ++i)
			{
				if(_rom[_offset + i] < 0x20 || _rom[_offset + i] >= 0x7f)
					return false;
			}
			return _rom[_offset + 0x0a] <= 12 && _rom[_offset + 0x0b] <= 12 && _rom[_offset + 0x0c] <= 15 &&
			       _rom[_offset + 0x0d] <= 1;
		}

		std::string trimmed(const uint8_t* _name, const size_t _size)
		{
			std::string name(reinterpret_cast<const char*>(_name), _size);
			const auto end = name.find_last_not_of(' ');
			return end == std::string::npos ? std::string() : name.substr(0, end + 1);
		}

		template<size_t N> bool readLayout(const std::vector<uint8_t>& _rom, const TimbreTable (&_layout)[N],
			std::array<std::vector<std::string>, 4>& _timbres)
		{
			std::array<std::vector<std::string>, 4> timbres;
			for(const auto& t : _layout)
			{
				for(uint8_t i = 0; i < t.count; ++i)
				{
					const auto at = t.table + i * 2u;
					if(at + 2 > _rom.size())
						return false;
					const auto offset = static_cast<size_t>(_rom[at] | _rom[at + 1] << 8) + t.base;
					if(!isTimbre(_rom, offset))
						return false;
					timbres[t.group].push_back(trimmed(&_rom[offset], 10));
				}
			}
			_timbres = std::move(timbres);
			return true;
		}

		// The tone table of a U-110 style wave image: 128 places of 50H bytes from 1000H, each starting with a ten
		// character name. The CM-32P's internal ROM leaves the places past its tones blank.
		constexpr size_t g_toneTable = 0x1000;
		constexpr size_t g_toneStride = 0x50;
		constexpr size_t g_tones = 128;
	}

	const std::string& NameCatalog::timbre(const uint8_t _group, const uint8_t _number) const
	{
		if(_group >= m_timbres.size() || _number >= m_timbres[_group].size())
			return g_none;
		return m_timbres[_group][_number];
	}

	const std::string& NameCatalog::pcmTone(const uint8_t _media, const uint8_t _number) const
	{
		if(_media >= m_pcmTones.size() || _number >= m_pcmTones[_media].size())
			return g_none;
		return m_pcmTones[_media][_number];
	}

	bool NameCatalog::readLaControl(const std::vector<uint8_t>& _control)
	{
		return readLayout(_control, g_newLayout, m_timbres) || readLayout(_control, g_oldLayout, m_timbres);
	}

	void NameCatalog::readPcmTones(const uint8_t _media, const uint8_t* _decoded, const size_t _size)
	{
		if(_media >= m_pcmTones.size())
			return;
		auto& tones = m_pcmTones[_media];
		tones.assign(g_tones, {});
		for(size_t i = 0; i < g_tones; ++i)
		{
			const auto offset = g_toneTable + i * g_toneStride;
			if(offset + 10 > _size)
				break;
			const auto* name = _decoded + offset;
			bool printable = true;
			for(size_t c = 0; c < 10; ++c)
				printable &= name[c] >= 0x20 && name[c] < 0x7f;
			if(printable)
				tones[i] = trimmed(name, 10);
		}
	}

	std::shared_ptr<const NameCatalog> loadNameCatalog(const DeviceModel _model, const std::vector<uint8_t>& _pcmCard,
		std::string& _error)
	{
		_error.clear();
		if(!isMt32Model(_model))
			return {};

		auto catalog = std::make_shared<NameCatalog>();
		if(hasLaBoard(_model))
		{
			// The CM-64's LA board is a CM-32L
			const auto set = RomLoader::findLaRomSet(_model == DeviceModel::Cm64 ? LaModel::Cm32l : toLaModel(_model));
			if(!set.isValid())
				_error = "The LA board's ROMs could not be read.";
			else if(!catalog->readLaControl(set.control))
				_error = "The timbre names were not found in the LA board's control ROM.";
		}
		if(hasPcmBoard(_model))
		{
			const auto set = RomLoader::findCm32pRomSet();
			if(!set.isValid())
			{
				_error = "The CM-32P's ROMs could not be read.";
			}
			else
			{
				// The tone table is at the start of the first wave ROM, which needs no more than its first block
				// decoded
				std::vector<uint8_t> decoded(rLib::rom::Wave19::BlockSize);
				rLib::rom::Wave19::descramble(set.waves[0].data(), set.waves[0].size(), decoded.data(), decoded.size());
				catalog->readPcmTones(0, decoded.data(), decoded.size());
			}
			if(const auto card = Cm32p::decodeCard(_pcmCard); !card.empty())
				catalog->readPcmTones(1, card.data(), card.size());
		}
		return catalog;
	}
}
