#pragma once

#include "juce_gui_basics/juce_gui_basics.h"

namespace genericUI
{
	// For whatever reason JUCE allows button clicks with the right mouse button.
	// https://forum.juce.com/t/fr-buttons-add-option-to-ignore-clicks-with-right-mouse-button/20723/11
	// And for whatever reason jules thinks this is a brilliant idea, although every other UI does not work like that
	// https://forum.juce.com/t/get-rid-of-right-click-clicking/23287/5

	template<typename T>
	class Button : public T
	{
	public:
		using T::T;
		using Callback = std::function<bool(const juce::MouseEvent&)>;

		void mouseDown(const juce::MouseEvent& _e) override
	    {
			if(onDown && onDown(_e))
				return;

	        if (!allowRightClick() && _e.mods.isPopupMenu())
	            return;

			T::mouseDown(_e);
	    }

	    void mouseDrag(const juce::MouseEvent& _e) override
	    {
	        if (!allowRightClick() && _e.mods.isPopupMenu())
	            return;

			T::mouseDrag(_e);
	    }

	    void mouseUp(const juce::MouseEvent& _e) override
	    {
			if(onUp && onUp(_e))
				return;

	        if (!allowRightClick() && _e.mods.isPopupMenu())
	            return;

			T::mouseUp(_e);
	    }

		void focusGained (juce::Component::FocusChangeType _type) override
		{
			if(!allowRightClick() && _type == juce::Component::focusChangedByMouseClick && juce::ModifierKeys::currentModifiers.isPopupMenu())
				return;
			T::focusGained(_type);
		}

		void allowRightClick(const bool _allow)
		{
			m_allowRightClick = _allow;
		}

		bool allowRightClick() const
		{
			return m_allowRightClick;
		}

		Callback onDown;
		Callback onUp;

	private:
		bool m_allowRightClick = false;
	};
}