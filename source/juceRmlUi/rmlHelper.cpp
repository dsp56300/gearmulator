#include "rmlHelper.h"

#include <cassert>

#include "Core/ElementStyle.h"

#include "juceRmlComponent.h"
#include "rmlDragSource.h"
#include "rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementInstancer.h"
#include "RmlUi/Core/ElementUtilities.h"
#include "RmlUi/Core/Event.h"
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
		MouseButton toRmlMouseButton(const juce::MouseEvent& _e)
		{
			if (_e.mods.isRightButtonDown())
				return MouseButton::Right;
			if (_e.mods.isMiddleButtonDown())
				return MouseButton::Middle;
			return MouseButton::Left;
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

		Rml::Vector2<float> getMousePos(const Rml::Event& _event)
		{
			const auto x = _event.GetParameter<float>("mouse_x", 0.0f);
			const auto y = _event.GetParameter<float>("mouse_y", 0.0f);

			return {x, y};
		}

		MouseButton getMouseButton(const Rml::Event& _event)
		{
			// RmlUi uses 0 for left, 1 for right, 2 for middle
			const int button = _event.GetParameter<int>("button", 0);
			return static_cast<MouseButton>(button);
		}

		Rml::Input::KeyIdentifier getKeyIdentifier(const Rml::Event& _event)
		{
			return static_cast<Rml::Input::KeyIdentifier>(_event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN));
		}

		Rml::Input::KeyModifier getKeyModifiers(const Rml::Event& _event)
		{
			const bool ctrl = _event.GetParameter<int>("ctrl_key", 0) > 0;
			const bool shift = _event.GetParameter<int>("shift_key", 0) > 0;
			const bool alt = _event.GetParameter<int>("alt_key", 0) > 0;
			const bool meta = _event.GetParameter<int>("meta_key", 0) > 0;
			const bool capslock = _event.GetParameter<int>("caps_lock_key", 0) > 0;
			const bool numlock = _event.GetParameter<int>("num_lock_key", 0) > 0;
			const bool scrolllock = _event.GetParameter<int>("scroll_lock_key", 0) > 0;

			uint8_t mods = 0;

			if (ctrl      ) mods |= Rml::Input::KeyModifier::KM_CTRL;
			if (shift     ) mods |= Rml::Input::KeyModifier::KM_SHIFT;
			if (alt       ) mods |= Rml::Input::KeyModifier::KM_ALT;
			if (meta      ) mods |= Rml::Input::KeyModifier::KM_META;
			if (capslock  ) mods |= Rml::Input::KeyModifier::KM_CAPSLOCK;
			if (numlock   ) mods |= Rml::Input::KeyModifier::KM_NUMLOCK;
			if (scrolllock) mods |= Rml::Input::KeyModifier::KM_SCROLLLOCK;

			return static_cast<Rml::Input::KeyModifier>(mods);
		}

		bool getKeyModCtrl(const Rml::Event& _event)
		{
			return _event.GetParameter<int>("ctrl_key", 0) > 0;
		}

		bool getKeyModShift(const Rml::Event& _event)
		{
			return _event.GetParameter<int>("shift_key", 0) > 0;
		}

		bool getKeyModAlt(const Rml::Event& _event)
		{
			return _event.GetParameter<int>("alt_key", 0) > 0;
		}

		Rml::Element* getDragElement(const Rml::Event& _event)
		{
			auto* dragElem = _event.GetParameter<void*>("drag_element", nullptr);
			return static_cast<Rml::Element*>(dragElem);
		}

		DragSource* getDragSource(const Rml::Event& _event)
		{
			auto* elem = getDragElement(_event);
			if (!elem)
				return nullptr;
			return DragSource::fromElement(elem);
		}

		bool isChildOf(const Rml::Element* _parent, const Rml::Element* _child)
		{
			if (!_parent || !_child)
				return false;
			while (_child)
			{
				if (_child == _parent)
					return true;
				_child = _child->GetParentNode();
			}
			return false;
		}

		bool changeProperty(Rml::Element* _element, Rml::PropertyId _propertyId, const Rml::Property& _property)
		{
			assert(_element);

			const auto* prop = _element->GetProperty(_propertyId);

			if (!prop || *prop != _property)
			{
				_element->SetProperty(_propertyId, _property);
				return true;
			}
			return false;
		}

		void changeAttribute(Rml::Element* _element, const std::string& _key, const std::string& _value)
		{
			assert(_element);

			auto* existingAttrib = _element->GetAttribute(_key);

			if (existingAttrib && existingAttrib->Get<Rml::String>() == _value)
				return;

			_element->SetAttribute(_key, _value);
		}

		Rml::ElementPtr clone(const Rml::Element* _element, Rml::ElementInstancer* _instancer)
		{
			// this is a copy of Rml::Element::Clone(), but supports another instancer
			if (!_instancer)
				return _element->Clone();

			auto clone = _instancer->InstanceElement(nullptr, _element->GetTagName(), _element->GetAttributes());
			if (!clone)
				return {};

			clone->SetInstancer(_instancer);

			// Copy over the attributes. The 'style' and 'class' attributes are skipped because inline styles and class names are copied manually below.
			// This is necessary in case any properties or classes have been set manually, in which case the 'style' and 'class' attributes are out of
			// sync with the used style and active classes.
			Rml::ElementAttributes clone_attributes = _element->GetAttributes();
			clone_attributes.erase("style");
			clone_attributes.erase("class");
			clone->SetAttributes(clone_attributes);

			for (auto& idProperty : _element->GetStyle()->GetLocalStyleProperties())
				clone->SetProperty(idProperty.first, idProperty.second);

			clone->GetStyle()->SetClassNames(_element->GetStyle()->GetClassNames());

			Rml::String innerRml;
			_element->GetInnerRML(innerRml);

			clone->SetInnerRML(innerRml);

			return clone;
		}

		Rml::Element* findChild(Rml::Element* _elem, const std::string& _name, const bool _mustExist)
		{
			auto* result = Rml::ElementUtilities::GetElementById(_elem, _name);

			if (!result && _mustExist)
				throw std::runtime_error("Element with id '" + _name + "' not found in '" + _elem->GetId() + "' (" + _elem->GetTagName() + ") element");

			return result;
		}

		void removeFromParent(Rml::Element* _elem)
		{
			if (!_elem)
				return;
			auto parentNode = _elem->GetParentNode();
			if (parentNode)
				parentNode->RemoveChild(_elem);
		}

		void callPostFrame(const std::function<void()>& _callback)
		{
			auto& comp = RmlInterfaces::getCurrentComponent();
			comp.addPostFrameCallback(_callback);
		}

		bool toBuffer(std::vector<uint8_t>& _buffer, juce::Image& _image)
		{
			const auto w = _image.getWidth();
			const auto h = _image.getHeight();

			const auto pixelCount = w * h;

			juce::Image::BitmapData bitmapData(_image, 0, 0, w, h, juce::Image::BitmapData::readOnly);

			uint32_t bufferIndex = 0;

			switch(_image.getFormat())
			{
			case juce::Image::ARGB:
				_buffer.resize(pixelCount * 4);
				for (int y=0; y<h; ++y)
				{
					for (int x=0; x<w; ++x)
					{
						// juce rturns an unpremultiplied color but we want a premultiplied pixel value
						// the function getPixelColour casts the pixel pointer to PixelARGB* but this
						// might change. Verify that its still the case and modify this assert accordingly
						static_assert(JUCE_MAJOR_VERSION == 7 && JUCE_MINOR_VERSION == 0);  // NOLINT(misc-redundant-expression)
						const auto pixel = reinterpret_cast<juce::PixelARGB*>(bitmapData.getPixelPointer(x, y));

						_buffer[bufferIndex++] = pixel->getRed();
						_buffer[bufferIndex++] = pixel->getGreen();
						_buffer[bufferIndex++] = pixel->getBlue();
						_buffer[bufferIndex++] = pixel->getAlpha();
					}
				}
				return true;
			case juce::Image::RGB:
				_buffer.resize(pixelCount * 3);
				for (int y=0; y<h; ++y)
				{
					for (int x=0; x<w; ++x)
					{
						auto pixel = bitmapData.getPixelColour(x,y);
						_buffer[bufferIndex++] = pixel.getRed();
						_buffer[bufferIndex++] = pixel.getGreen();
						_buffer[bufferIndex++] = pixel.getBlue();
					}
				}
				return true;
			case juce::Image::SingleChannel:
				{
					_buffer.resize(pixelCount * 3);

					uint8_t* pixelPtr = bitmapData.data;
					for (int i=0; i<pixelCount; ++i, ++pixelPtr)
					{
						_buffer[bufferIndex++] = *pixelPtr;
						_buffer[bufferIndex++] = *pixelPtr;
						_buffer[bufferIndex++] = *pixelPtr;
					}
				}
				return true;
			default:
				Rml::Log::Message(Rml::Log::LT_ERROR, "Unsupported image format: %d", static_cast<int>(_image.getFormat()));
				assert(false && "unsupported image format");
				return false;
			}
		}

		Rml::Colourb toRmlColor(const juce::Colour& _color)
		{
			return {
				_color.getRed(),
				_color.getGreen(),
				_color.getBlue(),
				_color.getAlpha()
			};
		}

		uint32_t toARGB(const Rml::Colourb& _color)
		{
			return (_color.alpha << 24) | (_color.red << 16) | (_color.green << 8) | _color.blue;
		}

		Rml::Colourb fromARGB(const uint32_t _color)
		{
			return {
				static_cast<uint8_t>((_color >> 16) & 0xFF),
				static_cast<uint8_t>((_color >> 8) & 0xFF),
				static_cast<uint8_t>(_color & 0xFF),
				static_cast<uint8_t>((_color >> 24) & 0xFF)
			};
		}
	}
}
