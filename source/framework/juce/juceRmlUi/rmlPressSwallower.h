#pragma once

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	// The press that dismisses a menu or a panel is for that only: whatever is under the pointer gets neither its
	// mousedown, its mouseup nor its click, as with a native menu. The caller stops the mousedown it is in; RmlUi clicks
	// the element the press began on when it is released, whatever became of the mousedown, so the rest of the press
	// is caught here. This outlives the menu, on the document, and goes with the click, with the next press if the
	// release was not a click, or with the document.
	void swallowRestOfPress(Rml::Element* _document);
}
