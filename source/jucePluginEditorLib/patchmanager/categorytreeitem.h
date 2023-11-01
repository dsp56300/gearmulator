#pragma once

#include "tagtreeitem.h"

namespace jucePluginEditorLib::patchManager
{
	class CategoryTreeItem : public TagTreeItem
	{
	public:
		CategoryTreeItem(PatchManager& _pm, const std::string& _category);

		bool mightContainSubItems() override
		{
			return false;
		}
	};
}
