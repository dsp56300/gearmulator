#pragma once

#include <memory>
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

	constexpr uint32_t g_invalidBank = ~0;
	constexpr uint32_t g_invalidProgram = ~0;

	using Data = std::vector<uint8_t>;
	using DataList = std::vector<Data>;
}
