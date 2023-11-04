#include "patch.h"

#include <cassert>
#include <sstream>

#include "patchmodifications.h"
#include "serialization.h"

namespace pluginLib::patchDB
{
	const TypedTags& Patch::getTags() const
	{
		if (!modifications)
			return tags;
		return modifications->mergedTags;
	}

	const std::string& Patch::getName() const
	{
		if (!modifications)
			return name;
		if (modifications->name.empty())
			return name;
		return modifications->name;
	}

	std::string PatchKey::toString() const
	{
		std::stringstream ss;

		if (source.type != SourceType::Invalid)
			ss << source.toString() << '|';

		if (program != g_invalidProgram)
			ss << "prog|" << program << '|';

		ss << "hash|" << hash;

		return ss.str();
	}

	PatchKey PatchKey::fromString(const std::string& _string)
	{
		const std::vector<std::string> elems = Serialization::split(_string, '|');

		if (elems.size() & 1)
			return {};

		PatchKey patchKey;

		for(size_t i=0; i<elems.size(); i+=2)
		{
			const auto& key = elems[i];
			const auto& val = elems[i + 1];

			if (key == "type")
				patchKey.source.type = toSourceType(val);
			else if (key == "name")
				patchKey.source.name = val;
			else if (key == "bank")
				patchKey.source.bank = ::strtol(val.c_str(), nullptr, 10);
			else if (key == "prog")
				patchKey.program = ::strtol(val.c_str(), nullptr, 10);
			else if (key == "hash")
				patchKey.hash = val;
			else
			{
				assert(false && "unknown property key while parsing patch key");
			}
		}

		return patchKey;
	}
}
