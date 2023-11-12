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

		PatchPtr patch;
		TypedTags tags;
		std::string name;

		// cache
		TypedTags mergedTags;
	};
}
