#pragma once

#include "tree.h"

namespace jucePluginEditorLib::patchManager
{
	class TagsTree : public Tree
	{
	public:
		explicit TagsTree(PatchManagerUiJuce& _pm);
	};
}
