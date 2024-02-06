#include "datasourcetree.h"

namespace jucePluginEditorLib::patchManager
{
	DatasourceTree::DatasourceTree(PatchManager& _pm): Tree(_pm)
	{
		addGroup(GroupType::Favourites);
		addGroup(GroupType::LocalStorage);
		addGroup(GroupType::DataSources);
	}
}
