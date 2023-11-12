#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

#include "datasource.h"
#include "tags.h"
#include "patchdbtypes.h"

namespace pluginLib::patchDB
{
	using PatchHash = std::array<uint8_t, 16>;

	struct Patch
	{
		Patch()
		{
			hash.fill(0);
		}

		Patch(const Patch&) = delete;
		Patch(Patch&&) = delete;

		virtual ~Patch() = default;

		Patch& operator = (const Patch&) = delete;
		Patch& operator = (Patch&&) = delete;

		std::string name;

		uint32_t bank = g_invalidBank;
		uint32_t program = g_invalidProgram;

		DataSourceNodePtr source;

		TypedTags tags;

		PatchHash hash;
		std::vector<uint8_t> sysex;

		std::weak_ptr<PatchModifications> modifications;

		const TypedTags& getTags() const;
		const std::string& getName() const;
	};

	struct PatchKey
	{
		DataSource source;
		PatchHash hash;
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

		bool isValid() const { return source.type != SourceType::Invalid; }

		std::string toString() const;
		static PatchKey fromString(const std::string& _string);
	};
}
