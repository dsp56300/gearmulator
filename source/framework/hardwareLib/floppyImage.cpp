#include "floppyImage.h"

#include <cstring>

namespace hwLib
{
	FloppyImage::FloppyImage(std::vector<uint8_t> _image, const uint32_t _sectorsPerTrack, std::string _path)
		: m_image(std::move(_image))
		, m_path(std::move(_path))
		, m_sectors(_sectorsPerTrack)
	{
		const size_t cylinderBytes = size_t(Heads) * m_sectors * SectorSize;
		if (cylinderBytes && m_image.size() % cylinderBytes == 0)
			m_cylinders = static_cast<uint32_t>(m_image.size() / cylinderBytes);
		if (m_cylinders < 40 || m_cylinders > 84)
		{
			m_cylinders = 0;
			m_image.clear();
		}
	}

	std::string FloppyImage::label() const
	{
		if (m_image.size() < 0x40)
			return {};
		std::string s;
		for (size_t i = 0x20; i < 0x40 && m_image[i]; ++i)
			s.push_back(m_image[i] >= 0x20 && m_image[i] < 0x7f ? static_cast<char>(m_image[i]) : ' ');
		while (!s.empty() && s.back() == ' ')
			s.pop_back();
		while (!s.empty() && s.front() == ' ')
			s.erase(s.begin());
		return s;
	}

	bool FloppyImage::readSector(const uint8_t _cylinder, const uint8_t _head, const uint8_t _sector, uint8_t* _out, const size_t _size) const
	{
		if (!isValid() || _cylinder >= m_cylinders || _head >= Heads || _sector < 1 || _sector > m_sectors || _size != SectorSize)
			return false;
		const size_t offset = (size_t(_cylinder) * Heads + _head) * m_sectors * SectorSize + size_t(_sector - 1) * SectorSize;
		std::memcpy(_out, m_image.data() + offset, SectorSize);
		return true;
	}
}
