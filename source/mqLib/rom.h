#pragma once

#include "wLib/wRom.h"

namespace mqLib
{
	class ROM final : public wLib::ROM
	{
	public:
		static constexpr uint32_t g_romSize = 524288;

		ROM() = default;
		explicit ROM(const std::string& _filename);
		explicit ROM(const std::vector<uint8_t>& _data, const std::string& _name);

		static constexpr uint32_t size() { return g_romSize; }

		uint32_t getSize() const override { return g_romSize; }

	private:
		void verifyRom();
	};
}
