#include "coPosition.h"

#include <sstream>

namespace rmlPlugin::skinConverter
{
	bool CoPosition::write(std::stringstream& _ss) const
	{
		if (!isValid())
			return false;

		_ss << "left: " << x << "dp; ";
		_ss << "top: " << y << "dp; ";
		_ss << "width: " << width << "dp; ";
		_ss << "height: " << height << "dp;";

		return true;
	}

	void CoPosition::set(const genericUI::UiObject& _object)
	{
		x = _object.getPropertyFloat("x", 0.0f);
		y = _object.getPropertyFloat("y", 0.0f);
		width = _object.getPropertyFloat("width", 0.0f);
		height = _object.getPropertyFloat("height", 0.0f);
	}
}
