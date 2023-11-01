#include "categorytreeitem.h"

namespace jucePluginEditorLib::patchManager
{
	CategoryTreeItem::CategoryTreeItem(PatchManager& _pm, const std::string& _category): TagTreeItem(_pm, GroupType::Categories, _category)
	{
	}
}
