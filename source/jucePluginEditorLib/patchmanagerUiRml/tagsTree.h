#pragma once

#include "tree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class TagsTree : public Tree
	{
	public:
		explicit TagsTree(PatchManagerUiRml& _pm, juceRmlUi::ElemTree* _tree);
	};
}
