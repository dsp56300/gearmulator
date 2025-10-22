#pragma once

#include "listmodel.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class Grid : public ListModel
	{
	public:
		explicit Grid(PatchManagerUiRml& _pm, juceRmlUi::ElemList* _list);
	};
}
