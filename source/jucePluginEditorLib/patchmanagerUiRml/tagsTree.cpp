#include "tagsTree.h"

#include "juceRmlUi/rmlElemTree.h"

namespace jucePluginEditorLib::patchManagerRml
{
	TagsTree::TagsTree(PatchManagerUiRml& _pm, juceRmlUi::ElemTree* _tree): Tree(_pm, _tree)
	{
		getTree()->getTree().setEnableMultiSelect(true);
	}
}
