#include "patch.h"

#include <cassert>
#include <sstream>

#include "patchmodifications.h"
#include "serialization.h"

#include "juce_core/juce_core.h"

namespace pluginLib::patchDB
{
	std::pair<PatchPtr, PatchModificationsPtr> Patch::createCopy(const DataSourceNodePtr& _ds) const
	{
		if (*_ds == *source)
			return {};

		auto p = std::shared_ptr<Patch>(new Patch(*this));

		p->source = _ds;

		PatchModificationsPtr newMods;

		if(const auto mods = modifications.lock())
			newMods = std::make_shared<PatchModifications>(*mods);

		if(newMods)
		{
			p->modifications = newMods;
			newMods->patch = p;
		}

		return { p, newMods };
	}

	const TypedTags& Patch::getTags() const
	{
		if (const auto m = modifications.lock())
			return m->mergedTags;
		return tags;
	}

	const Tags& Patch::getTags(const TagType _type) const
	{
		return getTags().get(_type);
	}

	const std::string& Patch::getName() const
	{
		const auto m = modifications.lock();
		if (!m)
			return name;
		if (m->name.empty())
			return name;
		return m->name;
	}

	std::string PatchKey::toString() const
	{
		std::stringstream ss;

		if (source.type != SourceType::Invalid)
			ss << source.toString() << '|';

		if (program != g_invalidProgram)
			ss << "prog|" << program << '|';

		ss << "hash|" << juce::String::toHexString(hash.data(), (int)hash.size(), 0);

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
			{
				juce::MemoryBlock mb;
				mb.loadFromHexString(juce::String(val));
				if(mb.getSize() == std::size(patchKey.hash))
				{
					memcpy(patchKey.hash.data(), mb.getData(), std::size(patchKey.hash));
				}
				else
				{
					assert(false && "hash value has invalid length");
					patchKey.hash.fill(0);
				}
			}
			else
			{
				assert(false && "unknown property key while parsing patch key");
			}
		}

		return patchKey;
	}
}
