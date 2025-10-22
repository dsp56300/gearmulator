#pragma once

#include "tree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class DatasourceTree : public Tree
	{
	public:
		explicit DatasourceTree(PatchManagerUiRml& _pm, juceRmlUi::ElemTree* _tree);
	};
}
