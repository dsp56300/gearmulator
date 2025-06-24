#pragma once
#include <sstream>

#include "juceUiLib/uiObject.h"

namespace rmlPlugin::skinConverter
{
	struct CoPosition
	{
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;

		void write(std::stringstream& _ss) const;

		void set(const genericUI::UiObject& _object);
	};
}
