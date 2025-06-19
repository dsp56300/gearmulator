#include "datasourcetree.h"

namespace jucePluginEditorLib::patchManager
{
	DatasourceTree::DatasourceTree(PatchManagerUiJuce& _pm, const std::initializer_list<GroupType>& _groupTypes) : Tree(_pm)
	{
		for (const auto& groupType : _groupTypes)
			addGroup(groupType);
	}
}
