#pragma once

#include <string>
#include <vector>
#include <cstddef>
#include <set>

namespace synthLib
{
	class RomLoader
	{
	public:
		static std::vector<std::string> findFiles(const std::string& _extension, size_t _minSize, size_t _maxSize);
		static std::vector<std::string> findFiles(const std::string& _path, const std::string& _extension, size_t _minSize, size_t _maxSize);

		static void addSearchPath(const std::string& _path);
	};
}
