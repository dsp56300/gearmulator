#include "tagstree.h"

namespace jucePluginEditorLib::patchManager
{
	TagsTree::TagsTree(PatchManager& _pm) : Tree(_pm)
	{
		addGroup(GroupType::Categories);
		addGroup(GroupType::Tags);

		setMultiSelectEnabled(true);
	}
}
