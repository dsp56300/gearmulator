#include "rmlHelper.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "RmlUi/Core/Input.h"

namespace juceRmlUi
{
	namespace helper
	{
		int toRmlModifiers(const juce::ModifierKeys _mods)
		{
			int rmlModifiers = 0;
			if (_mods.isCtrlDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_CTRL;
			if (_mods.isShiftDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_SHIFT;
			if (_mods.isAltDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_ALT;
			if (_mods.isCommandDown())
				rmlModifiers |= Rml::Input::KeyModifier::KM_META;
			return rmlModifiers;
		}
		int toRmlModifiers(const juce::MouseEvent& _e)
		{
			return toRmlModifiers(_e.mods);
		}
		int toRmlModifiers(const juce::KeyPress& _e)
		{
			return toRmlModifiers(_e.getModifiers());
		}
		int toRmlMouseButton(const juce::MouseEvent& _e)
		{
			if (_e.mods.isRightButtonDown())
				return 1;
			if (_e.mods.isMiddleButtonDown())
				return 2;
			return 0;
		}
		Rml::Input::KeyIdentifier toRmlKey(const juce::KeyPress& _key)
		{
			const auto keyCode = _key.getKeyCode();
		    if (keyCode == juce::KeyPress::spaceKey) return Rml::Input::KI_SPACE;
		    if (keyCode == juce::KeyPress::returnKey) return Rml::Input::KI_RETURN;
		    if (keyCode == juce::KeyPress::escapeKey) return Rml::Input::KI_ESCAPE;
		    if (keyCode == juce::KeyPress::backspaceKey) return Rml::Input::KI_BACK;
		    if (keyCode == juce::KeyPress::deleteKey) return Rml::Input::KI_DELETE;
		    if (keyCode == juce::KeyPress::insertKey) return Rml::Input::KI_INSERT;
		    if (keyCode == juce::KeyPress::tabKey) return Rml::Input::KI_TAB;
		    if (keyCode == juce::KeyPress::leftKey) return Rml::Input::KI_LEFT;
		    if (keyCode == juce::KeyPress::rightKey) return Rml::Input::KI_RIGHT;
		    if (keyCode == juce::KeyPress::upKey) return Rml::Input::KI_UP;
		    if (keyCode == juce::KeyPress::downKey) return Rml::Input::KI_DOWN;
		    if (keyCode == juce::KeyPress::homeKey) return Rml::Input::KI_HOME;
		    if (keyCode == juce::KeyPress::endKey) return Rml::Input::KI_END;
		    if (keyCode == juce::KeyPress::pageUpKey) return Rml::Input::KI_PRIOR;
		    if (keyCode == juce::KeyPress::pageDownKey) return Rml::Input::KI_NEXT;
		    if (keyCode == juce::KeyPress::F1Key) return Rml::Input::KI_F1;
		    if (keyCode == juce::KeyPress::F2Key) return Rml::Input::KI_F2;
		    if (keyCode == juce::KeyPress::F3Key) return Rml::Input::KI_F3;
		    if (keyCode == juce::KeyPress::F4Key) return Rml::Input::KI_F4;
		    if (keyCode == juce::KeyPress::F5Key) return Rml::Input::KI_F5;
		    if (keyCode == juce::KeyPress::F6Key) return Rml::Input::KI_F6;
		    if (keyCode == juce::KeyPress::F7Key) return Rml::Input::KI_F7;
		    if (keyCode == juce::KeyPress::F8Key) return Rml::Input::KI_F8;
		    if (keyCode == juce::KeyPress::F9Key) return Rml::Input::KI_F9;
		    if (keyCode == juce::KeyPress::F10Key) return Rml::Input::KI_F10;
		    if (keyCode == juce::KeyPress::F11Key) return Rml::Input::KI_F11;
		    if (keyCode == juce::KeyPress::F12Key) return Rml::Input::KI_F12;
		    if (keyCode == juce::KeyPress::F13Key) return Rml::Input::KI_F13;
		    if (keyCode == juce::KeyPress::F14Key) return Rml::Input::KI_F14;
		    if (keyCode == juce::KeyPress::F15Key) return Rml::Input::KI_F15;
		    if (keyCode == juce::KeyPress::F16Key) return Rml::Input::KI_F16;
		    if (keyCode == juce::KeyPress::F17Key) return Rml::Input::KI_F17;
		    if (keyCode == juce::KeyPress::F18Key) return Rml::Input::KI_F18;
		    if (keyCode == juce::KeyPress::F19Key) return Rml::Input::KI_F19;
		    if (keyCode == juce::KeyPress::F20Key) return Rml::Input::KI_F20;
		    if (keyCode == juce::KeyPress::F21Key) return Rml::Input::KI_F21;
		    if (keyCode == juce::KeyPress::F22Key) return Rml::Input::KI_F22;
		    if (keyCode == juce::KeyPress::F23Key) return Rml::Input::KI_F23;
		    if (keyCode == juce::KeyPress::F24Key) return Rml::Input::KI_F24;
		    if (keyCode == juce::KeyPress::numberPad0) return Rml::Input::KI_NUMPAD0;
		    if (keyCode == juce::KeyPress::numberPad1) return Rml::Input::KI_NUMPAD1;
		    if (keyCode == juce::KeyPress::numberPad2) return Rml::Input::KI_NUMPAD2;
		    if (keyCode == juce::KeyPress::numberPad3) return Rml::Input::KI_NUMPAD3;
		    if (keyCode == juce::KeyPress::numberPad4) return Rml::Input::KI_NUMPAD4;
		    if (keyCode == juce::KeyPress::numberPad5) return Rml::Input::KI_NUMPAD5;
		    if (keyCode == juce::KeyPress::numberPad6) return Rml::Input::KI_NUMPAD6;
		    if (keyCode == juce::KeyPress::numberPad7) return Rml::Input::KI_NUMPAD7;
		    if (keyCode == juce::KeyPress::numberPad8) return Rml::Input::KI_NUMPAD8;
		    if (keyCode == juce::KeyPress::numberPad9) return Rml::Input::KI_NUMPAD9;
		    if (keyCode == juce::KeyPress::numberPadAdd) return Rml::Input::KI_ADD;
		    if (keyCode == juce::KeyPress::numberPadSubtract) return Rml::Input::KI_SUBTRACT;
		    if (keyCode == juce::KeyPress::numberPadMultiply) return Rml::Input::KI_MULTIPLY;
		    if (keyCode == juce::KeyPress::numberPadDivide) return Rml::Input::KI_DIVIDE;
		    if (keyCode == juce::KeyPress::numberPadSeparator) return Rml::Input::KI_SEPARATOR;
		    if (keyCode == juce::KeyPress::numberPadDecimalPoint) return Rml::Input::KI_DECIMAL;
		    if (keyCode == juce::KeyPress::numberPadEquals) return Rml::Input::KI_OEM_NEC_EQUAL;
		    if (keyCode == juce::KeyPress::numberPadDelete) return Rml::Input::KI_DELETE;
		    if (keyCode == juce::KeyPress::playKey) return Rml::Input::KI_PLAY;
		    if (keyCode == juce::KeyPress::stopKey) return Rml::Input::KI_MEDIA_STOP;
		    if (keyCode == juce::KeyPress::fastForwardKey) return Rml::Input::KI_MEDIA_NEXT_TRACK;
		    if (keyCode == juce::KeyPress::rewindKey) return Rml::Input::KI_MEDIA_PREV_TRACK;
		    if (keyCode == '0') return Rml::Input::KI_0;
		    if (keyCode == '1') return Rml::Input::KI_1;
		    if (keyCode == '2') return Rml::Input::KI_2;
		    if (keyCode == '3') return Rml::Input::KI_3;
		    if (keyCode == '4') return Rml::Input::KI_4;
		    if (keyCode == '5') return Rml::Input::KI_5;
		    if (keyCode == '6') return Rml::Input::KI_6;
		    if (keyCode == '7') return Rml::Input::KI_7;
		    if (keyCode == '8') return Rml::Input::KI_8;
		    if (keyCode == '9') return Rml::Input::KI_9;
		    if (keyCode == 'a' || keyCode == 'A') return Rml::Input::KI_A;
		    if (keyCode == 'b' || keyCode == 'B') return Rml::Input::KI_B;
		    if (keyCode == 'c' || keyCode == 'C') return Rml::Input::KI_C;
		    if (keyCode == 'd' || keyCode == 'D') return Rml::Input::KI_D;
		    if (keyCode == 'e' || keyCode == 'E') return Rml::Input::KI_E;
		    if (keyCode == 'f' || keyCode == 'F') return Rml::Input::KI_F;
		    if (keyCode == 'g' || keyCode == 'G') return Rml::Input::KI_G;
		    if (keyCode == 'h' || keyCode == 'H') return Rml::Input::KI_H;
		    if (keyCode == 'i' || keyCode == 'I') return Rml::Input::KI_I;
		    if (keyCode == 'j' || keyCode == 'J') return Rml::Input::KI_J;
		    if (keyCode == 'k' || keyCode == 'K') return Rml::Input::KI_K;
		    if (keyCode == 'l' || keyCode == 'L') return Rml::Input::KI_L;
		    if (keyCode == 'm' || keyCode == 'M') return Rml::Input::KI_M;
		    if (keyCode == 'n' || keyCode == 'N') return Rml::Input::KI_N;
		    if (keyCode == 'o' || keyCode == 'O') return Rml::Input::KI_O;
		    if (keyCode == 'p' || keyCode == 'P') return Rml::Input::KI_P;
		    if (keyCode == 'q' || keyCode == 'Q') return Rml::Input::KI_Q;
		    if (keyCode == 'r' || keyCode == 'R') return Rml::Input::KI_R;
		    if (keyCode == 's' || keyCode == 'S') return Rml::Input::KI_S;
		    if (keyCode == 't' || keyCode == 'T') return Rml::Input::KI_T;
		    if (keyCode == 'u' || keyCode == 'U') return Rml::Input::KI_U;
		    if (keyCode == 'v' || keyCode == 'V') return Rml::Input::KI_V;
		    if (keyCode == 'w' || keyCode == 'W') return Rml::Input::KI_W;
		    if (keyCode == 'x' || keyCode == 'X') return Rml::Input::KI_X;
		    if (keyCode == 'y' || keyCode == 'Y') return Rml::Input::KI_Y;
		    if (keyCode == 'z' || keyCode == 'Z') return Rml::Input::KI_Z;
		    return Rml::Input::KI_UNKNOWN;
		}		
	}
}
