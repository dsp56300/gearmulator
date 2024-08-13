#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace n2x
{
	template<uint32_t Size>
	class RomData
	{
	public:
		static constexpr uint32_t MySize = Size;
		RomData();
		RomData(const std::string& _filename);

		bool isValid() const { return !m_filename.empty(); }
		const auto& data() const { return m_data; }
		auto& data() { return m_data; }

		void saveAs(const std::string& _filename);

		const auto& getFilename() const { return m_filename; }

		void invalidate()
		{
			m_filename.clear();
		}
	private:
		std::array<uint8_t, Size> m_data;
		std::string m_filename;
	};
}
