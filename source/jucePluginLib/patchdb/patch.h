#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "datasource.h"
#include "tags.h"
#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	struct Patch
	{
		virtual ~Patch() = default;

		std::string name;

		uint32_t bank = g_invalidBank;
		uint32_t program = g_invalidProgram;

		std::shared_ptr<DataSource> source;

		TypedTags tags;

		std::string hash;
		std::vector<uint8_t> sysex;

		PatchModificationsPtr modifications;
	};

	struct PatchKey
	{
		std::shared_ptr<DataSource> source;
		std::string hash;

		explicit PatchKey(const Patch& _patch) : source(_patch.source), hash(_patch.hash) {}

		bool operator == (const PatchKey& _other) const
		{
			return *source == *_other.source && hash == _other.hash;
		}

		bool operator != (const PatchKey& _other) const
		{
			return !(*this == _other);
		}

		bool operator < (const PatchKey& _other) const
		{
			if (*source < *_other.source)
				return true;
			if (*source > *_other.source)
				return false;
			if (hash < _other.hash)
				return true;
			return false;
		}
	};
}
