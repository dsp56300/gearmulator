#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace nmm
{
	std::string sha256(const std::vector<uint8_t>& data);
	class Rom
	{
	public:
		static constexpr uint32_t Base = 0x100000;
		static constexpr uint32_t Size = 0x566e0;
		explicit Rom(const std::string& filename);
		const std::vector<uint8_t>& data() const { return m_data; }
		uint32_t word(uint32_t offset) const;
		std::vector<uint32_t> resident() const;
	private:
		std::vector<uint8_t> m_data;
	};
}
