#include "datasourceTree.h"

#include "juceRmlUi/rmlElemTree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	DatasourceTree::DatasourceTree(PatchManagerUiRml& _pm, juceRmlUi::ElemTree* _tree)
	: Tree(_pm, _tree)
	{
		_tree->getTree().setAllowDeselectOnSecondClick(false);
	}
}
