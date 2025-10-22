#pragma once

#include "coStyle.h"
#include "coAttribs.h"
#include "coPosition.h"
#include "juceUiLib/uiObject.h"

namespace rmlPlugin::skinConverter
{
	struct ConvertedObject
	{
	public:
		std::string tag = "div";
		std::string id;
		std::vector<std::string> classes;
		CoStyle style;
		CoPosition position;
		CoAttribs attribs;
		std::string innerText;
		std::vector<ConvertedObject> children;

		void write(std::stringstream& _ss, size_t _depth);
		void set(const std::string& _id, const genericUI::UiObject& _object);
	};
}
