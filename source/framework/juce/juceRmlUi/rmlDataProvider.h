#pragma once

#include <string>
#include <cstdint>

namespace juceRmlUi
{
	class DataProvider
	{
	public:
		virtual ~DataProvider() = default;

		virtual const char* getResourceByFilename(const std::string& _name, uint32_t& _dataSize) = 0;
		virtual std::vector<std::string> getAllFilenames() = 0;
	};
}
