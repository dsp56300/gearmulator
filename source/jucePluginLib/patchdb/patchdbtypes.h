#pragma once

#include <memory>

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
}
