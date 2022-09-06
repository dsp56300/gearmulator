#pragma once

#include <string>
#include <vector>

namespace mqLib
{
	class ROM
	{
	public:
		static constexpr uint32_t g_romSize = 524288;

		explicit ROM(const std::string& _filename, const uint8_t* _data = nullptr) : m_data(_data)
		{
			if (!_filename.empty())
				loadFromFile(_filename);
		}

		const uint8_t* getData() const { return m_data; }
		constexpr uint32_t getSize() const { return g_romSize; }

	private:
		bool loadFromFile(const std::string& _filename);

		const uint8_t* m_data;
		std::vector<uint8_t> m_buffer;
	};
}
