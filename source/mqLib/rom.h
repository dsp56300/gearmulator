#pragma once

#include <string>
#include <vector>

namespace mqLib
{
	class ROM
	{
	public:
		explicit ROM(const std::string& _filename);

		const std::vector<uint8_t>& getData() const { return m_data; }
		size_t getSize() const { return m_data.size(); }

	private:
		std::vector<uint8_t> m_data;
	};
}
