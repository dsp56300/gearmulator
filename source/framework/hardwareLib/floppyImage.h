#pragma once

#include "upd765.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hwLib
{
	// A raw sector image of a double-sided 3.5" disk: cylinders x 2 heads x
	// sectors x 512 bytes, cylinder-major, the layout Roland's own disk formats
	// and a plain dump of them share. 2DD has 9 sectors per track (737,280
	// bytes for 80 cylinders), 2HD 18 (1,474,560). The drive cannot write it.
	class FloppyImage final : public Upd765::Disk
	{
	public:
		static constexpr uint32_t Heads = 2;
		static constexpr uint32_t SectorSize = 512;
		static constexpr uint32_t SectorsDD = 9;
		static constexpr uint32_t SectorsHD = 18;

		FloppyImage() = default;
		// An image whose size is not a whole number of 40 to 84 cylinders of
		// `_sectorsPerTrack` sectors is rejected: isValid() returns false.
		FloppyImage(std::vector<uint8_t> _image, uint32_t _sectorsPerTrack, std::string _path = {});

		bool isValid() const { return m_cylinders != 0; }
		uint32_t cylinders() const { return m_cylinders; }
		uint32_t sectorsPerTrack() const { return m_sectors; }
		const std::vector<uint8_t>& data() const { return m_image; }
		const std::string& path() const { return m_path; }
		// The volume label Roland writes at offset 0x20 ("MV-30 System Disk
		// Ver.1.0", "S-760 System Disk Ver. 2.24" and the like), or empty.
		std::string label() const;

		bool present() const override { return isValid(); }
		bool readSector(uint8_t _cylinder, uint8_t _head, uint8_t _sector, uint8_t* _out, size_t _size) const override;

	private:
		std::vector<uint8_t> m_image;
		std::string m_path;
		uint32_t m_sectors = 0;
		uint32_t m_cylinders = 0;
	};
}
