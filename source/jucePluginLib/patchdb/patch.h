#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "datasource.h"

namespace pluginLib::patchDB
{
	struct Patch
	{
		virtual ~Patch() = default;

		std::string name;
		uint8_t bank = 0;
		uint8_t program = 0;

		DataSource source;

		std::vector<std::string> categories;
		std::vector<std::string> tags;
		std::string hash;
		std::vector<uint8_t> sysex;
	};

	struct PatchKey
	{
		DataSource source;
		std::string hash;

		explicit PatchKey(const Patch& _patch) : source(_patch.source), hash(_patch.hash) {}

		bool operator == (const PatchKey& _other) const
		{
			return source == _other.source && hash == _other.hash;
		}

		bool operator != (const PatchKey& _other) const
		{
			return !(*this == _other);
		}

		bool operator < (const PatchKey& _other) const
		{
			if (source < _other.source)
				return true;
			if (source > _other.source)
				return false;
			if (hash < _other.hash)
				return true;
			return false;
		}
	};
}
