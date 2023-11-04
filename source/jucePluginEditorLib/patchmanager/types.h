#pragma once

namespace pluginLib::patchDB
{
	enum class TagType;
}

namespace jucePluginEditorLib::patchManager
{
	enum class GroupType
	{
		DataSources,
		Categories,
		Tags,
		Favourites,
		Count
	};

	pluginLib::patchDB::TagType toTagType(GroupType _groupType);
}
