#include "synthLibTests.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "baseLib/filesystem.h"

namespace
{
	std::vector<std::string> sortedNames(const std::vector<std::string>& _paths)
	{
		std::vector<std::string> names;
		names.reserve(_paths.size());
		for (const auto& p : _paths)
			names.push_back(std::filesystem::u8path(p).filename().u8string());
		std::sort(names.begin(), names.end());
		return names;
	}

	void writeBytes(const std::filesystem::path& _file, const size_t _count)
	{
		std::ofstream out(_file, std::ios::binary);
		out << std::string(_count, 'x');
	}
}

// findFiles lists one folder. Without an extension and without size limits it returns every entry, subfolders
// included - the patch manager builds the children of a folder data source from exactly that. A size range asks for
// files: a folder has no size, so it never matches one, not even a range that starts at 0 - the DSP Bridge server
// reads everything its ROM search returns.
void testFilesystem()
{
	std::cout << "Testing baseLib::filesystem::findFiles..." << std::endl;

	const auto root = std::filesystem::temp_directory_path() / "synthLibTests_findFiles";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root / "subfolder");
	writeBytes(root / "patch.syx", 10);
	writeBytes(root / "notes.txt", 100);

	const auto rootPath = root.u8string();

	std::vector<std::string> everything;
	baseLib::filesystem::findFiles(everything, rootPath, {}, 0, 0);

	std::vector<std::string> byExtension;
	baseLib::filesystem::findFiles(byExtension, rootPath, ".syx", 0, 0);

	std::vector<std::string> upTo50;
	baseLib::filesystem::findFiles(upTo50, rootPath, {}, 0, 50);

	std::vector<std::string> from50;
	baseLib::filesystem::findFiles(from50, rootPath, {}, 50, 0);

	std::filesystem::remove_all(root);

	TEST_ASSERT(sortedNames(everything) == std::vector<std::string>({"notes.txt", "patch.syx", "subfolder"}));
	TEST_ASSERT(sortedNames(byExtension) == std::vector<std::string>({"patch.syx"}));
	TEST_ASSERT(sortedNames(upTo50) == std::vector<std::string>({"patch.syx"}));
	TEST_ASSERT(sortedNames(from50) == std::vector<std::string>({"notes.txt"}));

	std::cout << "  findFiles tests passed" << std::endl;
}
