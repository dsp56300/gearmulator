#include "tagtreeitem.h"

#include "patchmanager.h"

namespace jucePluginEditorLib::patchManager
{
	TagTreeItem::TagTreeItem(PatchManagerUiJuce& _pm, const GroupType _type, const std::string& _tag) : TreeItem(_pm, _tag), m_group(_type), m_tag(_tag)
	{
	}
}
