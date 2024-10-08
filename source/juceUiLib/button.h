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

		virtual void mouseDown(const juce::MouseEvent& _e) override
	    {
			if(onDown && onDown(_e))
				return;

	        if (!allowRightClick() && _e.mods.isPopupMenu())
	            return;

			T::mouseDown(_e);
	    }

	    virtual void mouseDrag(const juce::MouseEvent& _e) override
	    {
	        if (!allowRightClick() && _e.mods.isPopupMenu())
	            return;

			T::mouseDrag(_e);
	    }

	    virtual void mouseUp(const juce::MouseEvent& _e) override
	    {
			if(onUp && onUp(_e))
				return;

			if(_e.mods.isPopupMenu())
			{
				if(!allowRightClick())
					return;
				m_isRightClick = true;
			}
			else
			{
				m_isRightClick = false;
			}

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

		bool hitTest (const int _x, const int _y) override
		{
			if(!T::hitTest(_x,_y))
				return false;

			if(_x < m_hitAreaOffset.getX() || _y < m_hitAreaOffset.getY())
				return false;
			if(_x > T::getWidth() - m_hitAreaOffset.getWidth() || _y > T::getHeight() - m_hitAreaOffset.getHeight())
				return false;
			return true;
		}

		void setHitAreaOffset(const juce::Rectangle<int>& _offset)
		{
			m_hitAreaOffset = _offset;
		}

		bool isRightClick() const
		{
			return m_isRightClick;
		}

		Callback onDown;
		Callback onUp;

	private:
		bool m_allowRightClick = false;
		bool m_isRightClick = false;
		juce::Rectangle<int> m_hitAreaOffset{0,0,0,0};
	};
}