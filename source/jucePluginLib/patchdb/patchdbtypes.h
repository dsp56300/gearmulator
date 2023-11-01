#pragma once

#include <memory>
#include <set>
#include <vector>

namespace pluginLib::patchDB
{
	enum class SourceType
	{
		Invalid,
		Rom,
		File,
		Folder
	};

	struct Patch;
	using PatchPtr = std::shared_ptr<Patch>;

	using Data = std::vector<uint8_t>;
	using DataList = std::vector<Data>;

	using SearchHandle = uint32_t;

	static constexpr SearchHandle g_invalidSearchHandle = ~0;
	static constexpr uint32_t g_invalidBank = ~0;
	static constexpr uint32_t g_invalidProgram = ~0;

	struct Dirty
	{
		bool dataSources = false;
		bool categories = false;
		bool tags = false;
		std::set<SearchHandle> searches;
		bool patches;
	};
}
