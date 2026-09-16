#pragma once

#include <functional>

#include "RmlUi/Core/Types.h"

namespace juce
{
	class PopupMenu;
}

namespace juceRmlUi
{
	class RmlComponent;

	// Lists shown in a juce popup window instead of inside the document, so they can extend past
	// the window's edges: combo boxes with popup="window" and Menu::openPopupWindow().
	namespace popupWindow
	{
		// Shows _menu dropping down from the area _position/_size of _component's document, in
		// document coordinates; a zero size anchors it to a point. _onResult gets the chosen item
		// id with RmlUi access held. It is not called when the list is dismissed, or once the
		// component has gone away.
		void show(juce::PopupMenu& _menu, RmlComponent& _component, const Rml::Vector2f& _position,
		          const Rml::Vector2f& _size, std::function<void(int)> _onResult);
	}
}
