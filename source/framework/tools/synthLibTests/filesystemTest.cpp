#include "synthLibTests.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "baseLib/filesystem.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

namespace
{
	// No <filesystem> in here. It is unavailable below macOS 10.15 while we build for 10.13, and
	// avoiding it is the whole reason baseLib::filesystem has its USE_DIRENT path. So the test drives
	// the API it tests, and borrows rmdir for the one thing that API has no counterpart for.
	void removeDirectory(const std::string& _dir)
	{
#ifdef _WIN32
		_rmdir(_dir.c_str());
#else
		rmdir(_dir.c_str());
#endif
	}

	std::vector<std::string> sortedNames(const std::vector<std::string>& _paths)
	{
		std::vector<std::string> names;
		names.reserve(_paths.size());
		for (const auto& p : _paths)
			names.push_back(baseLib::filesystem::getFilenameWithoutPath(p));
		std::sort(names.begin(), names.end());
		return names;
	}

	void writeBytes(const std::string& _file, const size_t _count)
	{
		const std::vector<uint8_t> data(_count, 'x');
		baseLib::filesystem::writeFile(_file, data);
	}
}

// findFiles lists one folder. Without an extension and without size limits it returns every entry, subfolders
// included - the patch manager builds the children of a folder data source from exactly that. A size range asks for
// files: a folder has no size, so it never matches one, not even a range that starts at 0 - the DSP Bridge server
// reads everything its ROM search returns.
void testFilesystem()
{
	std::cout << "Testing baseLib::filesystem::findFiles..." << std::endl;

	const auto root = baseLib::filesystem::validatePath(baseLib::filesystem::getCurrentDirectory()) + "synthLibTests_findFiles";
	const auto subfolder = root + "/subfolder";
	const auto syxFile = root + "/patch.syx";
	const auto txtFile = root + "/notes.txt";

	auto cleanup = [&]()
	{
		baseLib::filesystem::remove(syxFile);
		baseLib::filesystem::remove(txtFile);
		removeDirectory(subfolder);
		removeDirectory(root);
	};

	cleanup();	// in case a previous run was killed before it got to clean up after itself

	baseLib::filesystem::createDirectory(subfolder);	// creates the root along with it
	writeBytes(syxFile, 10);
	writeBytes(txtFile, 100);

	std::vector<std::string> everything;
	baseLib::filesystem::findFiles(everything, root, {}, 0, 0);

	std::vector<std::string> byExtension;
	baseLib::filesystem::findFiles(byExtension, root, ".syx", 0, 0);

	std::vector<std::string> upTo50;
	baseLib::filesystem::findFiles(upTo50, root, {}, 0, 50);

	std::vector<std::string> from50;
	baseLib::filesystem::findFiles(from50, root, {}, 50, 0);

	cleanup();

	TEST_ASSERT(sortedNames(everything) == std::vector<std::string>({"notes.txt", "patch.syx", "subfolder"}));
	TEST_ASSERT(sortedNames(byExtension) == std::vector<std::string>({"patch.syx"}));
	TEST_ASSERT(sortedNames(upTo50) == std::vector<std::string>({"patch.syx"}));
	TEST_ASSERT(sortedNames(from50) == std::vector<std::string>({"notes.txt"}));

	std::cout << "  findFiles tests passed" << std::endl;
}
