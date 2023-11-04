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

		DataSourceNodePtr source;

		TypedTags tags;

		std::string hash;
		std::vector<uint8_t> sysex;

		PatchModificationsPtr modifications;

		const TypedTags& getTags() const;
		const std::string& getName() const;
	};

	struct PatchKey
	{
		DataSource source;
		std::string hash;
		uint32_t program = g_invalidProgram;

		PatchKey() = default;

		explicit PatchKey(const Patch& _patch) : source(static_cast<const DataSource&>(*_patch.source)), hash(_patch.hash), program(_patch.program) {}

		bool operator == (const PatchKey& _other) const
		{
			return source == _other.source && hash == _other.hash && program == _other.program;
		}

		bool operator != (const PatchKey& _other) const
		{
			return !(*this == _other);
		}

		bool operator < (const PatchKey& _other) const
		{
			if (program < _other.program)
				return true;
			if (program > _other.program)
				return false;
			if (source < _other.source)
				return true;
			if (source > _other.source)
				return false;
			if (hash < _other.hash)
				return true;
			return false;
		}

		bool isValid() const { return !hash.empty() && source.type != SourceType::Invalid; }

		std::string toString() const;
		static PatchKey fromString(const std::string& _string);
	};
}
