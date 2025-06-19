#pragma once

#include "tree.h"

namespace jucePluginEditorLib::patchManager
{
	class DatasourceTree : public Tree
	{
	public:
		explicit DatasourceTree(PatchManagerUiJuce& _pm, const std::initializer_list<GroupType>& _groupTypes);
	};
}
