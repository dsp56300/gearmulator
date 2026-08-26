#pragma once

#include "listmodel.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class List : public ListModel
	{
	public:
		explicit List(PatchManagerUiRml& _pm, juceRmlUi::ElemList* _list);
	};
}
