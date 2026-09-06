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

		bool isValid() const { return x != 0.0f || y != 0.0f || width > 0.0f || height > 0.0f; }

		bool write(std::stringstream& _ss) const;

		void set(const genericUI::UiObject& _object);
	};
}
