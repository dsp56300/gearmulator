#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88emuplayer/ui/Emu88EditorLcd.h"
#include "88emuplayer/ui/Emu88EditorPlaylist.h"
#include "88emuplayer/ui/Emu88PartEditor.h"
#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlElemValue.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "RmlUi/Core/ElementDocument.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <set>

namespace emu88Player
{
	using namespace editor;

	void Editor::wirePanel()
	{
		juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
		auto* document = m_rml->getDocument();
		updateDeviceSkin(m_processor.deviceModel());
		updatePowerVisuals();
		if(auto* lcd = document->GetElementById("hardwareLcd"))
		{
			m_lcd = std::make_unique<HardwareLcd>(*lcd);
			m_lcd->reset(m_processor.deviceModel());
		}
		if(auto* lcd = document->GetElementById("hardwareLcd2"))
		{
			m_lcd2 = std::make_unique<HardwareLcd>(*lcd);
			m_lcd2->reset(m_processor.deviceModel(), 1);
		}
		m_playlistEntries = document->GetElementById("playlistEntries");
		m_playerPlayGraphic = document->GetElementById("playerPlayGraphic");
		m_playerPauseGraphic = document->GetElementById("playerPauseGraphic");
		if(auto* playlist = document->GetElementById("playlist"))
		{
			m_playlistDropTarget = std::make_unique<PlaylistDropTarget>(playlist,
				[this](const size_t _from, const size_t _to)
				{
					if(m_processor.midiPlayer().move(_from, _to))
						saveDefaultPlaylist();
				},
				[this](const std::vector<std::string>& _files) { addMidiFiles(_files); },
				[this] { return m_processor.midiPlayer().entries().size(); });
			juceRmlUi::EventListener::Add(playlist, Rml::EventId::Mousedown, [this](Rml::Event& _event)
			{
				if(!juceRmlUi::helper::isContextMenu(_event))
					return;
				_event.StopPropagation();
				openPlaylistContextMenu(_event);
			});
		}
		if(auto* add = document->GetElementById("btPlaylistAdd"))
			juceRmlUi::EventListener::Add(add, Rml::EventId::Click,
				[this](Rml::Event&) { chooseMidiFiles(); });
		if(auto* toggle = document->GetElementById("btPlayerToggle"))
			juceRmlUi::EventListener::Add(toggle, Rml::EventId::Click,
				[this](Rml::Event&) { m_processor.midiPlayer().togglePlayPause(); });
		if(auto* stop = document->GetElementById("btPlayerStop"))
			juceRmlUi::EventListener::Add(stop, Rml::EventId::Click,
				[this](Rml::Event&) { m_processor.midiPlayer().stop(); });
		if(auto* device = document->GetElementById("btDeviceSelect"))
			juceRmlUi::EventListener::Add(device, Rml::EventId::Click,
				[this](Rml::Event& _event) { openDeviceMenu(_event); });
		refreshPlaylist();
		updatePlayerVisuals(m_processor.midiPlayer().status());

		juceRmlUi::EventListener::Add(document, Rml::EventId::Mousedown, [this](Rml::Event& _event)
		{
			if(!juceRmlUi::helper::isContextMenu(_event))
				return;
			_event.StopPropagation();
			openContextMenu(_event);
		});

		// A double-click on a Sound Canvas's display opens the part editor. The display lets the pointer through
		// to the panel, so the document checks where it was.
		juceRmlUi::EventListener::Add(document, Rml::EventId::Dblclick, [this](Rml::Event& _event)
		{
			if(!hasPartEditor(m_processor.deviceModel()) || !m_rml)
				return;
			auto* lcd = m_rml->getDocument()->GetElementById("hardwareLcd");
			if(!lcd || !lcd->IsVisible() || !lcd->IsPointWithinElement(juceRmlUi::helper::getMousePos(_event)))
				return;
			_event.StopPropagation();
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					editor->showPartEditor(true);
			});
		});

		const auto wireButton = [this, document](const ButtonBinding& _binding)
		{
			if(!_binding.element || std::string_view(_binding.element) == "bt8850Value")
				return;
			if(auto* element = document->GetElementById(_binding.element))
			{
				const auto button = static_cast<uint32_t>(_binding.button);
				juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown,
					[this, button](Rml::Event& _event)
					{
						if(!juceRmlUi::helper::isContextMenu(_event))
							setPointerButton(button, true);
					});
			}
		};
		// The SK-88Pro shows the SC-88Pro's faces at its own matrix positions, so those faces
		// look their switch up in the table of the board running when clicked.
		for(const auto& binding : g_sc88Buttons)
		{
			auto* element = document->GetElementById(binding.element);
			if(!element)
				continue;
			const std::string id = binding.element;
			const auto proButton = static_cast<uint32_t>(binding.button);
			juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown,
				[this, id, proButton](Rml::Event& _event)
				{
					if(juceRmlUi::helper::isContextMenu(_event))
						return;
					if(m_processor.deviceModel() != emu88Lib::DeviceModel::Sk88Pro)
					{
						setPointerButton(proButton, true);
						return;
					}
					for(const auto& sk : g_sk88Buttons)
						if(sk.element && id == sk.element)
							setPointerButton(static_cast<uint32_t>(sk.button), true);
				});
		}
		for(const auto& binding : g_sc8850Buttons) wireButton(binding);
		for(const auto& binding : g_sc880Buttons) wireButton(binding);
		// The MV-30's faces hold their switch while the button is down, past the 32 the other panels have
		for(const auto& binding : g_mv30Buttons)
		{
			auto* element = binding.element ? document->GetElementById(binding.element) : nullptr;
			if(!element)
				continue;
			const auto bit = uint64_t{1} << binding.button;
			juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown, [this, bit](Rml::Event& _event)
			{
				if(juceRmlUi::helper::isContextMenu(_event))
					return;
				m_mv30PointerButtons |= bit;
				sendButtons();
				updateButtonVisuals();
			});
		}
		for(const auto& [id, detent] : {std::pair{"btMv30Inc", 1}, std::pair{"btMv30Dec", -1}})
		{
			if(auto* element = document->GetElementById(id))
				juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown, [this, detent = detent](Rml::Event& _event)
				{
					if(juceRmlUi::helper::isContextMenu(_event))
						return;
					if(auto* hardware = m_processor.hardware())
						hardware->turnPanelEncoder(detent);
					m_mv30Repeat = detent;
					m_mv30RepeatTicks = 0;
					updateButtonVisuals();
				});
		}
		// The SC-8850's VALUE encoder and the SC-880's VALUE dial, each with its push switch.
		for(const auto& [id, push] : {std::pair{"bt8850Value", static_cast<uint32_t>(emu88Lib::Sc8850Button::ValuePush)},
		                              std::pair{"bt880Value", static_cast<uint32_t>(emu88Lib::Sc880Button::DialPush)}})
		{
			auto* value = document->GetElementById(id);
			if(!value)
				continue;
			// Push + turn is the SC-8850 firmware's coarse mode: with ENCSW held every
			// detent steps the value by ten (four detents moved the instrument
			// from 001 to 041 on the board). So a drag must turn the encoder
			// without pushing it. Only a click that did not move pushes, as a
			// pulse long enough for the panel scan, started after the
			// document-wide mouseup has released the pointer buttons.
			juceRmlUi::EventListener::Add(value, Rml::EventId::Mousedown, [this](Rml::Event& _event)
			{
				const auto position = juceRmlUi::helper::getMousePos(_event);
				m_valueKnobDownX = position.x;
				m_valueKnobDownY = position.y;
			});
			juceRmlUi::EventListener::Add(value, Rml::EventId::Mouseup, [this, valuePush = push](Rml::Event& _event)
			{
				if(juceRmlUi::helper::isContextMenu(_event))
					return;
				const auto position = juceRmlUi::helper::getMousePos(_event);
				const auto dx = position.x - m_valueKnobDownX;
				const auto dy = position.y - m_valueKnobDownY;
				if(dx * dx + dy * dy > kValuePushClickRadius * kValuePushClickRadius)
					return;
				const juce::WeakReference<Editor> safeThis(this);
				// Outside the event, so the button visuals change under the access lock themselves.
				juce::Timer::callAfterDelay(1, [safeThis, valuePush]
				{
					if(auto* editor = safeThis.get())
					{
						juceRmlUi::RmlInterfaces::ScopedAccess access(editor->m_interfaces);
						editor->setPointerButton(valuePush, true);
					}
				});
				juce::Timer::callAfterDelay(1 + kValuePushPulseMs, [safeThis, valuePush]
				{
					if(auto* editor = safeThis.get())
					{
						juceRmlUi::RmlInterfaces::ScopedAccess access(editor->m_interfaces);
						editor->setPointerButton(valuePush, false);
					}
				});
			});
		}
		refreshButtonElements(m_processor.deviceModel());
		juceRmlUi::EventListener::Add(document, Rml::EventId::Mouseup, [this](Rml::Event&)
		{
			m_pointerButtons = 0;
			m_mv30PointerButtons = 0;
			m_mv30Repeat = 0;
			sendButtons();
			updateButtonVisuals();
		});
		juceRmlUi::EventListener::Add(document, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
			// The MV-30 has a key for every switch MAME binds one to, and Q is none of them: it has no power key
			if(m_processor.deviceModel() == emu88Lib::DeviceModel::Mv30)
			{
				if(handleMv30Key(key, true))
					_event.StopPropagation();
				return;
			}
			if(key == Rml::Input::KI_Q && !juceRmlUi::helper::getKeyModCtrl(_event) &&
			   !juceRmlUi::helper::getKeyModCommand(_event) && !juceRmlUi::helper::getKeyModAlt(_event))
			{
				if(!m_powerKeyDown) pressPowerKey(juceRmlUi::helper::getKeyModShift(_event));
				m_powerKeyDown = true;
				_event.StopPropagation();
				return;
			}
			if(key == Rml::Input::KI_F5 && m_processor.config().getBoolValue("reloadSkinViaF5", false))
			{
				_event.StopPropagation();
				const juce::WeakReference<Editor> safeThis(this);
				juce::MessageManager::callAsync([safeThis]
				{
					if(auto* editor = safeThis.get())
					{
						const auto skin = editor->m_skin;
						editor->loadSkin(skin);
					}
				});
				return;
			}
			const auto handleKey = [this, key, &_event](const auto& _bindings)
			{
				for(const auto& binding : _bindings)
				{
					if(binding.key != key)
						continue;
					setKeyboardButton(static_cast<uint32_t>(binding.button), true);
					_event.StopPropagation();
					return;
				}
			};
			const auto model = m_processor.deviceModel();
			if(emu88Lib::hasPanelKnob(model) && (key == g_mt32KnobUpKey || key == g_mt32KnobDownKey))
			{
				// The knob is a potentiometer: every press moves it a notch and it stays there.
				if(auto* hardware = m_processor.hardware())
					hardware->turnPanelEncoder(key == g_mt32KnobUpKey ? 1 : -1);
				_event.StopPropagation();
				return;
			}
			if(model == emu88Lib::DeviceModel::Sc8850)
				handleKey(g_sc8850Buttons);
			else if(usesLaBoardButtons(model))
				handleKey(g_mt32Buttons);
			else if(model == emu88Lib::DeviceModel::Sk88Pro)
				handleKey(g_sk88Buttons);
			else if(model == emu88Lib::DeviceModel::Sc880)
				handleKey(g_sc880Buttons);
			else
				handleKey(g_sc88Buttons);
		});
		juceRmlUi::EventListener::Add(document, Rml::EventId::Keyup, [this](Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
			if(m_processor.deviceModel() == emu88Lib::DeviceModel::Mv30)
			{
				if(handleMv30Key(key, false))
					_event.StopPropagation();
				return;
			}
			if(key == Rml::Input::KI_Q)
			{
				m_powerKeyDown = false;
				// Only Q holds that matrix position there; on the other boards it is some other key.
				if(emu88Lib::getPowerSwitch(m_processor.deviceModel()) == emu88Lib::PowerSwitch::Standby)
					setKeyboardButton(g_powerSwitchButton, false);
				_event.StopPropagation();
				return;
			}
			const auto handleKey = [this, key, &_event](const auto& _bindings)
			{
				for(const auto& binding : _bindings)
				{
					if(binding.key != key)
						continue;
					setKeyboardButton(static_cast<uint32_t>(binding.button), false);
					_event.StopPropagation();
					return;
				}
			};
			const auto model = m_processor.deviceModel();
			if(model == emu88Lib::DeviceModel::Sc8850)
				handleKey(g_sc8850Buttons);
			else if(usesLaBoardButtons(model))
				handleKey(g_mt32Buttons);
			else if(model == emu88Lib::DeviceModel::Sk88Pro)
				handleKey(g_sk88Buttons);
			else if(model == emu88Lib::DeviceModel::Sc880)
				handleKey(g_sc880Buttons);
			else
				handleKey(g_sc88Buttons);
		});

		if(auto* volume = document->GetElementById("outputVolume"))
		{
			volume->SetAttribute("min", g_volumeMinimum);
			volume->SetAttribute("max", g_volumeMaximum);
			volume->SetAttribute("step", 1);
			volume->SetAttribute("default", g_volumeUnity);
			// Seeded from the restored gain, not from unity: wirePanel also runs
			// on a skin reload and a GUI-scale change, either of which would
			// otherwise throw the user's setting away. false = no change event,
			// so seeding never writes back.
			juceRmlUi::ElemValue::setValue(volume, std::clamp(
				m_processor.outputGain() * static_cast<float>(g_volumeUnity),
				static_cast<float>(g_volumeMinimum), static_cast<float>(g_volumeMaximum)), false);
			juceRmlUi::EventListener::Add(volume, Rml::EventId::Change, [this, volume](Rml::Event&)
			{
				const auto value = std::clamp(juceRmlUi::ElemValue::getValue(volume),
				                              static_cast<float>(g_volumeMinimum),
				                              static_cast<float>(g_volumeMaximum));
				m_processor.setOutputGain(value / static_cast<float>(g_volumeUnity));
			});
		}
		for(const auto* id : {"bt8850Value", "bt880Value"})
		{
			auto* value = document->GetElementById(id);
			if(!value)
				continue;
			// The endless knob wraps by (max - min), so value 32 is the same
			// detent as value 0: 32 detents per turn, one knurl sprite per detent.
			constexpr int encoderMinimum = 0;
			constexpr int encoderMaximum = 32;
			constexpr int encoderCentre = 16;
			value->SetAttribute("min", encoderMinimum);
			value->SetAttribute("max", encoderMaximum);
			value->SetAttribute("step", 1);
			value->SetAttribute("default", encoderCentre);
			juceRmlUi::ElemValue::setValue(value, static_cast<float>(encoderCentre), false);
			if(auto* knob = dynamic_cast<juceRmlUi::ElemKnob*>(value))
				knob->setEndless(true);
			juceRmlUi::EventListener::Add(value, Rml::EventId::Change,
				[this, value, last = encoderCentre](Rml::Event&) mutable
				{
					const auto current = static_cast<int>(std::lround(juceRmlUi::ElemValue::getValue(value)));
					auto delta = current - last;
					constexpr int encoderRange = encoderMaximum - encoderMinimum;
					if(delta > encoderRange / 2) delta -= encoderRange;
					else if(delta < -encoderRange / 2) delta += encoderRange;
					last = current;
					if(auto* hardware = m_processor.hardware())
						hardware->turnPanelEncoder(delta);
				});
		}
		refreshLedElements(m_processor.deviceModel());
	}

	bool Editor::handleMv30Key(const int _key, const bool _down)
	{
		// The value dial and MAME's VALUE -/+ keys: a detent per press, and per repeat of a key held
		if(_key == g_mv30DialDownKey || _key == g_mv30DialUpKey || _key == g_mv30ValueDownKey ||
		   _key == g_mv30ValueUpKey)
		{
			const bool up = _key == g_mv30DialUpKey || _key == g_mv30ValueUpKey;
			if(_down)
				if(auto* hardware = m_processor.hardware())
					hardware->turnPanelEncoder(up ? 1 : -1);
			// The skin's + or - shows the press
			const uint8_t bit = up ? 2 : 1;
			m_mv30DialKeys = _down ? m_mv30DialKeys | bit : m_mv30DialKeys & ~bit;
			updateButtonVisuals();
			return true;
		}
		if(_key == g_mv30FootswitchKey)
		{
			m_mv30Footswitch = _down;
			sendButtons();
			return true;
		}
		for(const auto& binding : g_mv30Buttons)
		{
			if(binding.key != _key || binding.key == Rml::Input::KI_UNKNOWN)
				continue;
			const auto bit = uint64_t{1} << binding.button;
			m_mv30Buttons = _down ? m_mv30Buttons | bit : m_mv30Buttons & ~bit;
			sendButtons();
			updateButtonVisuals();
			return true;
		}
		return false;
	}

	void Editor::pressPowerKey(const bool _supply)
	{
		// Where the firmware reads the POWER switch, Q is that key, held as long as Q is: the board
		// stays on its supply and goes in and out of standby itself, taking any other keys held with
		// it. Shift+Q is the supply, which is what Q switches on every other board.
		if(!_supply && m_processor.hasValidRom() &&
		   emu88Lib::getPowerSwitch(m_processor.deviceModel()) == emu88Lib::PowerSwitch::Standby)
		{
			setKeyboardButton(g_powerSwitchButton, true);
			return;
		}
		togglePower();
	}

	void Editor::togglePower()
	{
		if(!m_processor.isPoweredOn())
		{
			selectDeviceModel(m_processor.deviceModel(), true, true);
			return;
		}
		m_processor.setPower(false);
		m_displayRevision = 0;
		updateButtonVisuals();
		updateLeds(0);
		updatePowerVisuals();
	}

	void Editor::updatePowerVisuals()
	{
		if(!m_rml) return;
		auto* document = m_rml->getDocument();
		const bool powered = m_processor.isPoweredOn();
		document->SetClass("powerOff", !powered);
		// A standby goes with the board that was in it. The next display snapshot says whether the
		// one running now has cut its display supply.
		document->SetClass("lcdUnpowered", false);
	}

	void Editor::setPointerButton(const uint32_t _button, const bool _pressed)
	{
		const auto mask = uint32_t{1} << _button;
		if(panelButtonsForDevice(m_processor.deviceModel(), mask) == 0) return;
		if(_pressed) m_pointerButtons |= mask;
		else m_pointerButtons &= ~mask;
		sendButtons();
		updateButtonVisuals();
	}

	void Editor::setKeyboardButton(const uint32_t _button, const bool _pressed)
	{
		const auto mask = uint32_t{1} << _button;
		if(panelButtonsForDevice(m_processor.deviceModel(), mask) == 0) return;
		if(_pressed) m_keyboardButtons |= mask;
		else m_keyboardButtons &= ~mask;
		sendButtons();
		updateButtonVisuals();
	}

	void Editor::sendButtons()
	{
		const auto mv30Buttons = m_mv30Buttons | m_mv30PointerButtons;
		if(m_processor.deviceModel() == emu88Lib::DeviceModel::Mv30 &&
		   (mv30Buttons != m_mv30SentButtons || m_mv30Footswitch != m_mv30SentFootswitch))
		{
			m_mv30SentButtons = mv30Buttons;
			m_mv30SentFootswitch = m_mv30Footswitch;
			if(auto* hardware = m_processor.hardware())
				hardware->setMv30Panel(mv30Buttons, m_mv30Footswitch);
		}
		const auto buttons = m_pointerButtons | m_keyboardButtons;
		if(buttons == m_sentButtons)
			return;
		m_sentButtons = buttons;
		if(auto* hardware = m_processor.hardware())
			hardware->setPanelButtons(panelButtonsForDevice(m_processor.deviceModel(), buttons));
	}

	void Editor::refreshButtonElements(const emu88Lib::DeviceModel _model)
	{
		m_buttonElements.fill(nullptr);
		if(!m_rml)
			return;
		auto* document = m_rml->getDocument();
		const auto add = [this, document](const ButtonBinding& _binding)
		{
			if(_binding.element && _binding.button < m_buttonElements.size())
				m_buttonElements[_binding.button] = document->GetElementById(_binding.element);
		};
		if(_model == emu88Lib::DeviceModel::Sc8850)
			for(const auto& binding : g_sc8850Buttons) add(binding);
		else if(usesLaBoardButtons(_model))
			for(const auto& binding : g_mt32Buttons) add(binding);
		else if(_model == emu88Lib::DeviceModel::Sc880)
			for(const auto& binding : g_sc880Buttons) add(binding);
		else if(_model == emu88Lib::DeviceModel::Sk88Pro)
		{
			// The SC-88Pro faces the SK-88Pro has no switch for stay on the artwork, disabled.
			for(const auto& binding : g_sc88Buttons)
				if(auto* element = document->GetElementById(binding.element))
					element->SetAttribute("disabled", "");
			for(const auto& binding : g_sk88Buttons) add(binding);
		}
		else if(_model != emu88Lib::DeviceModel::Mv30)
			for(const auto& binding : g_sc88Buttons) add(binding);
		m_mv30Elements.clear();
		m_mv30IncElement = m_mv30DecElement = nullptr;
		if(_model == emu88Lib::DeviceModel::Mv30)
		{
			for(const auto& binding : g_mv30Buttons)
				if(auto* element = binding.element ? document->GetElementById(binding.element) : nullptr)
					m_mv30Elements.emplace_back(element, uint64_t{1} << binding.button);
			m_mv30IncElement = document->GetElementById("btMv30Inc");
			m_mv30DecElement = document->GetElementById("btMv30Dec");
		}
		for(uint32_t button = 0; button < m_buttonElements.size(); ++button)
			if(auto* element = m_buttonElements[button])
			{
				if(panelButtonsForDevice(_model, uint32_t{1} << button) == 0)
					element->SetAttribute("disabled", "");
				else
					element->RemoveAttribute("disabled");
			}
	}

	void Editor::refreshLedElements(const emu88Lib::DeviceModel _model)
	{
		m_leds.fill(nullptr);
		if(!m_rml)
			return;
		auto* document = m_rml->getDocument();
		const auto assign = [this, document](const std::initializer_list<const char*> _ids)
		{
			size_t index = 0;
			for(const auto* id : _ids)
				m_leds[index++] = document->GetElementById(id);
		};
		switch(_model)
		{
		case emu88Lib::DeviceModel::Sc8850:
			// emu88Lib::Sc8850::Led bit order.
			assign({"bt8850Edit", "bt8850Drum", "bt8850Effects", "bt8850Shift", "bt8850Solo", "bt8850Mute"});
			break;
		case emu88Lib::DeviceModel::Sc880:
			// emu88Lib::Sc880Led bit order.
			assign({"bt880Utility", "bt880System", "bt880Edit", "bt880Mfx", "bt880Perf", "bt880Patch", "bt880Tone",
			        "bt880Rhythm", "bt880Write", "bt880Info"});
			break;
		case emu88Lib::DeviceModel::Sc55Mk2:
		case emu88Lib::DeviceModel::Sc55Mk1:
		case emu88Lib::DeviceModel::Sc55St:
		case emu88Lib::DeviceModel::Cm300:
		case emu88Lib::DeviceModel::Scc1a:
		case emu88Lib::DeviceModel::Scb55:
		case emu88Lib::DeviceModel::Rlp3237:
		case emu88Lib::DeviceModel::Sc155:
		case emu88Lib::DeviceModel::Sc155Mk2:
			// The only two lamps on an SC-55-family panel, in board bit order.
			assign({"btAll", "btMute"});
			break;
		case emu88Lib::DeviceModel::Cm32p:
		case emu88Lib::DeviceModel::Cm32l:
		case emu88Lib::DeviceModel::Cm32ln:
		case emu88Lib::DeviceModel::Cm64:
		case emu88Lib::DeviceModel::Mt32Old:
		case emu88Lib::DeviceModel::Mt32New:
			// The bezel's only driven lamp, bit 0 of the board's leds(); POWER is painted on.
			assign({"ledCmMidi"});
			break;
		default:
			// Gate-array LED port bit order, shared by SC-88, SC-88VL and SC-88Pro.
			assign({"btAll", "btMute", "btSc55Map", "btSc88Map", "ledSelectBottom", "ledSelectMiddle",
			        "ledSelectTop", "ledEfx"});
			break;
		}
	}

	void Editor::updateButtonVisuals()
	{
		const auto mv30Buttons = m_mv30Buttons | m_mv30PointerButtons;
		for(const auto& [element, bit] : m_mv30Elements)
			element->SetClass("pressed", (mv30Buttons & bit) != 0);
		if(m_mv30IncElement)
			m_mv30IncElement->SetClass("pressed", m_mv30Repeat > 0 || (m_mv30DialKeys & 2));
		if(m_mv30DecElement)
			m_mv30DecElement->SetClass("pressed", m_mv30Repeat < 0 || (m_mv30DialKeys & 1));
		const auto buttons = m_pointerButtons | m_keyboardButtons;
		for(uint32_t button = 0; button < m_buttonElements.size(); ++button)
			if(m_buttonElements[button])
				m_buttonElements[button]->SetClass("pressed", (buttons & (uint32_t{1} << button)) != 0);
	}

	void Editor::updateLeds(const uint16_t _value)
	{
		for(uint8_t i = 0; i < m_leds.size(); ++i)
			if(m_leds[i])
				m_leds[i]->SetClass("on", (_value & (uint16_t{1} << i)) != 0);

		if(auto* efx = m_leds[7])
		{
			const auto model = m_processor.deviceModel();
			const bool pro = model == emu88Lib::DeviceModel::Sc88Pro || model == emu88Lib::DeviceModel::VeGsPro;
			efx->SetClass("green", pro && (_value & 0x80));
			efx->SetClass("red", pro && (_value & 0x100));
			if(pro)
				efx->SetClass("on", (_value & 0x180) != 0);
		}
	}

	KeyboardShortcutGroups Editor::buildMv30KeyboardGroups()
	{
		// The keys MAME has for the panel, in the panel's sections; the switches it has none for are not listed
		const auto key = [](const Mv30Button _button)
		{
			for(const auto& binding : g_mv30Buttons)
				if(binding.button == static_cast<uint8_t>(_button))
					return keyName(binding.key);
			return std::string();
		};
		const auto rows = [&](const std::initializer_list<Mv30Button> _buttons)
		{
			std::vector<KeyboardShortcut> result;
			for(const auto button : _buttons)
				for(const auto& binding : g_mv30Buttons)
					if(binding.button == static_cast<uint8_t>(button) && binding.key != Rml::Input::KI_UNKNOWN)
						result.push_back({keyName(binding.key), binding.label});
			return result;
		};
		KeyboardShortcutGroups groups;
		groups.push_back({"Modes", rows({Mv30Button::PatternEdit, Mv30Button::TrackEdit, Mv30Button::System,
		                                 Mv30Button::Disk})});
		groups.push_back({"Function", rows({Mv30Button::F1, Mv30Button::F2, Mv30Button::F3, Mv30Button::F4,
		                                    Mv30Button::F5})});
		auto cursor = std::vector<KeyboardShortcut>{{key(Mv30Button::Up) + " / " + key(Mv30Button::Down) + " / " +
			key(Mv30Button::Left) + " / " + key(Mv30Button::Right), "Cursor"}};
		for(auto& row : rows({Mv30Button::Enter, Mv30Button::Exit, Mv30Button::Jump}))
			cursor.push_back(std::move(row));
		groups.push_back({"Cursor", std::move(cursor)});
		auto value = std::vector<KeyboardShortcut>{
			{keyName(g_mv30DialDownKey) + " / " + keyName(g_mv30DialUpKey), "Value dial, a detent"},
			{keyName(g_mv30ValueDownKey) + " / " + keyName(g_mv30ValueUpKey), "VALUE - / +, held repeats"},
			{key(Mv30Button::Num0) + " - " + key(Mv30Button::Num9), "Numeric keys"}};
		for(auto& row : rows({Mv30Button::Insert, Mv30Button::Delete, Mv30Button::Shift}))
			value.push_back(std::move(row));
		groups.push_back({"Value", std::move(value)});
		groups.push_back({"Sequencer", rows({Mv30Button::StartStop})});
		groups.push_back({"Footswitch", {{keyName(g_mv30FootswitchKey), "Footswitch, held"}}});
		return groups;
	}

	KeyboardShortcutGroups Editor::buildKeyboardGroups()
	{
		const auto model = m_processor.deviceModel();
		const auto is8850 = model == emu88Lib::DeviceModel::Sc8850;
		const auto isLa = usesLaBoardButtons(model);
		const auto isSk = model == emu88Lib::DeviceModel::Sk88Pro;
		const auto is880 = model == emu88Lib::DeviceModel::Sc880;

		const auto keyFor = [is8850, isLa, isSk, is880](const uint8_t _button) -> std::string
		{
			const auto find = [_button](const auto& _table) -> std::string
			{
				for(const auto& binding : _table)
					if(binding.button == _button)
						return keyName(binding.key);
				return {};
			};
			return is8850 ? find(g_sc8850Buttons) : isLa ? find(g_mt32Buttons)
			     : isSk ? find(g_sk88Buttons) : is880 ? find(g_sc880Buttons) : find(g_sc88Buttons);
		};
		const auto populated = [model, is8850](const uint8_t _button)
		{
			const auto bit = uint32_t{1} << _button;
			return is8850 ? bit != 0 : panelButtonsForDevice(model, bit) != 0;
		};

		if(model == emu88Lib::DeviceModel::Mv30)
			return buildMv30KeyboardGroups();

		KeyboardShortcutGroups groups{{"Power", {{"Q", "Power on / off"}}}};
		if(emu88Lib::getPowerSwitch(model) == emu88Lib::PowerSwitch::Standby)
			groups.front().rows = {{"Q", "POWER: standby / on"}, {"Shift + Q", "Power supply off / on"}};
		if(emu88Lib::hasPanelKnob(model))
			groups.push_back({"Knob", {{keyName(g_mt32KnobUpKey) + " / " + keyName(g_mt32KnobDownKey),
			                            "VOLUME / VALUE knob up / down, a 32nd of its travel"}}});
		const auto walk = [&](const auto& _layout)
		{
			// The heading is carried until a row of its group survives: on a
			// board that lacks the group's first switch the heading would
			// otherwise be dropped and the rest fall into the previous group.
			const char* pendingGroup = nullptr;
			for(const auto& helpRow : _layout)
			{
				if(helpRow.group)
					pendingGroup = helpRow.group;
				const bool pairRow = helpRow.second != kHelpNone;
				if(!populated(helpRow.first) || (pairRow && !populated(helpRow.second)))
					continue;
				if(helpRow.proOnly && model != emu88Lib::DeviceModel::Sc88Pro)
					continue;
				std::string keys;
				if(helpRow.keys)
				{
					keys = helpRow.keys;
				}
				else
				{
					keys = keyFor(helpRow.first);
					if(keys.empty())
						continue;
					if(pairRow)
					{
						auto right = keyFor(helpRow.second);
						if(right.empty())
							continue;
						keys += " / " + right;
					}
				}
				if(pendingGroup || groups.empty())
				{
					groups.push_back({pendingGroup ? pendingGroup : "", {}});
					pendingGroup = nullptr;
				}
				groups.back().rows.push_back({std::move(keys), helpRow.label});
			}
		};
		if(is8850)
			walk(g_sc8850Help);
		else if(isLa)
			walk(g_mt32Help);
		else if(isSk)
			walk(g_sk88Help);
		else if(is880)
			walk(g_sc880Help);
		else
			walk(g_sc88Help);

		// A group whose switches this board has none of leaves an empty heading.
		groups.erase(std::remove_if(groups.begin(), groups.end(),
			[](const KeyboardShortcutGroup& _group) { return _group.rows.empty(); }), groups.end());
		return groups;
	}
}
