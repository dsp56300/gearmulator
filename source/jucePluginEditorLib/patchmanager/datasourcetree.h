#pragma once

#include "tree.h"

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTree : public Tree
	{
	public:
		explicit DatasourceTree(PatchManager& _pm, const std::initializer_list<GroupType>& _groupTypes);
	};
}
