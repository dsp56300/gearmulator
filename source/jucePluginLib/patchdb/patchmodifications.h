#pragma once

#include "tags.h"

namespace juce
{
	class var;
	class DynamicObject;
}

namespace pluginLib::patchDB
{
	struct PatchModifications
	{
		bool modifyTags(const TypedTags& _tags);
		void updateCache();

		juce::DynamicObject* serialize() const;
		bool deserialize(const juce::var& _var);

		bool empty() const;

		void write(synthLib::BinaryStream& _outStream) const;
		bool read(synthLib::BinaryStream& _binaryStream);

		std::weak_ptr<Patch> patch;
		TypedTags tags;
		std::string name;

		// cache
		TypedTags mergedTags;
	};
}
