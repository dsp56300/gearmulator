#include "patch.h"

#include <cassert>
#include <sstream>

#include "db.h"
#include "patchmodifications.h"
#include "serialization.h"

#include "juce_core/juce_core.h"

#include "baseLib/binarystream.h"

namespace pluginLib::patchDB
{
	std::pair<PatchPtr, PatchModificationsPtr> Patch::createCopy(const DataSourceNodePtr& _ds) const
	{
		const auto patchDs = source.lock();
		if (patchDs && *_ds == *patchDs)
			return {};

		auto p = std::shared_ptr<Patch>(new Patch(*this));

		p->source = _ds->weak_from_this();
		_ds->patches.insert(p);

		PatchModificationsPtr newMods;

		if(const auto mods = modifications)
			newMods = std::make_shared<PatchModifications>(*mods);

		if(newMods)
			DB::assign(p, newMods);

		return { p, newMods };
	}

	void Patch::replaceData(const Patch& _patch)
	{
		name = _patch.name;
		tags = _patch.tags;
		hash = _patch.hash;
		sysex = _patch.sysex;
	}

	void Patch::write(baseLib::BinaryStream& _s) const
	{
		baseLib::ChunkWriter chunkWriter(_s, chunks::g_patch, 2);

		_s.write(name);
		_s.write(bank);
		_s.write(program);

		if(modifications && !modifications->empty())
		{
			_s.write<uint8_t>(1);
			modifications->write(_s);
		}
		else
		{
			_s.write<uint8_t>(0);
		}
		tags.write(_s);

		_s.write(hash);
		_s.write(sysex);
	}

	bool Patch::read(baseLib::BinaryStream& _in)
	{
		auto in = _in.tryReadChunk(chunks::g_patch, 2);
		if(!in)
			return false;

		name = in.readString();
		bank = in.read<uint32_t>();
		program = in.read<uint32_t>();

		const auto hasMods = in.read<uint8_t>();

		if(hasMods)
		{
			modifications = std::make_shared<PatchModifications>();
			if(!modifications->read(in))
				return false;
		}

		if(!tags.read(in))
			return false;

		hash = in.read<PatchHash>();
		in.read(sysex);

		return true;
	}

	bool Patch::operator==(const PatchKey& _key) const
	{
		return program == _key.program && hash == _key.hash && PatchKey::equals(source.lock(), _key.source);
	}

	const TypedTags& Patch::getTags() const
	{
		if (const auto m = modifications)
			return m->mergedTags;
		return tags;
	}

	const Tags& Patch::getTags(const TagType _type) const
	{
		return getTags().get(_type);
	}

	const std::string& Patch::getName() const
	{
		const auto m = modifications;
		if (!m || m->name.empty())
			return name;
		return m->name;
	}

	std::string PatchKey::toString(const bool _includeDatasource) const
	{
		if(!isValid())
			return {};

		std::stringstream ss;

		if (_includeDatasource && source->type != SourceType::Invalid)
			ss << source->toString() << '|';

		if (program != g_invalidProgram)
			ss << "prog|" << program << '|';

		ss << "hash|" << juce::String::toHexString(hash.data(), (int)hash.size(), 0);

		return ss.str();
	}

	PatchKey PatchKey::fromString(const std::string& _string, const DataSourceNodePtr& _dataSource/* = nullptr*/)
	{
		const std::vector<std::string> elems = Serialization::split(_string, '|');

		if (elems.size() & 1)
			return {};

		PatchKey patchKey;

		if(_dataSource)
			patchKey.source = _dataSource;
		else
			patchKey.source = std::make_shared<DataSourceNode>();

		for(size_t i=0; i<elems.size(); i+=2)
		{
			const auto& key = elems[i];
			const auto& val = elems[i + 1];

			if (key == "type")
				patchKey.source->type = toSourceType(val);
			else if (key == "name")
				patchKey.source->name = val;
			else if (key == "bank")
				patchKey.source->bank = ::strtol(val.c_str(), nullptr, 10);
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
