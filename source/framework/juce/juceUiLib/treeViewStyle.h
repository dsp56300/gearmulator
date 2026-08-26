#pragma once

#include "uiObjectStyle.h"

namespace genericUI
{
	class TreeViewStyle : public UiObjectStyle
	{
	public:
		TreeViewStyle(Editor& _editor) : UiObjectStyle(_editor) {}

		void apply(Editor& _editor, const UiObject& _object) override;

		void apply(juce::TreeView& _target) const;

		bool boldRootItems() const { return m_boldRootItems; }

	private:
		bool m_boldRootItems = true;
	};
}
