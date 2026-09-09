#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "Emu88EditorLcd.h"
#include "Emu88EditorPlaylist.h"
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
		if(auto* lcd = document->GetElementById("hardwareLcd"))
		{
			m_lcd = std::make_unique<HardwareLcd>(*lcd);
			m_lcd->reset(m_processor.deviceModel());
		}
		m_playlistEntries = document->GetElementById("playlistEntries");
		m_playerPlayGraphic = document->GetElementById("playerPlayGraphic");
		m_playerPauseGraphic = document->GetElementById("playerPauseGraphic");
		if(auto* playlist = document->GetElementById("playlist"))
		{
			m_playlistDropTarget = std::make_unique<PlaylistDropTarget>(playlist,
				[this](const size_t _from, const size_t _to) { m_processor.midiPlayer().move(_from, _to); },
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
		for(const auto& binding : g_sc88Buttons) wireButton(binding);
		for(const auto& binding : g_sc8850Buttons) wireButton(binding);
		if(auto* value = document->GetElementById("bt8850Value"))
		{
			// Push + turn is the firmware's coarse mode: with ENCSW held every
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
			juceRmlUi::EventListener::Add(value, Rml::EventId::Mouseup, [this](Rml::Event& _event)
			{
				if(juceRmlUi::helper::isContextMenu(_event))
					return;
				const auto position = juceRmlUi::helper::getMousePos(_event);
				const auto dx = position.x - m_valueKnobDownX;
				const auto dy = position.y - m_valueKnobDownY;
				if(dx * dx + dy * dy > kValuePushClickRadius * kValuePushClickRadius)
					return;
				constexpr auto valuePush = static_cast<uint32_t>(emu88Lib::Sc8850Button::ValuePush);
				const juce::WeakReference<Editor> safeThis(this);
				juce::Timer::callAfterDelay(1, [safeThis]
				{
					if(auto* editor = safeThis.get())
						editor->setPointerButton(valuePush, true);
				});
				juce::Timer::callAfterDelay(1 + kValuePushPulseMs, [safeThis]
				{
					if(auto* editor = safeThis.get())
						editor->setPointerButton(valuePush, false);
				});
			});
		}
		refreshButtonElements(m_processor.deviceModel());
		juceRmlUi::EventListener::Add(document, Rml::EventId::Mouseup, [this](Rml::Event&)
		{
			m_pointerButtons = 0;
			sendButtons();
			updateButtonVisuals();
		});
		juceRmlUi::EventListener::Add(document, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
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
			if(m_processor.deviceModel() == emu88Lib::DeviceModel::Sc8850)
				handleKey(g_sc8850Buttons);
			else
				handleKey(g_sc88Buttons);
		});
		juceRmlUi::EventListener::Add(document, Rml::EventId::Keyup, [this](Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
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
			if(m_processor.deviceModel() == emu88Lib::DeviceModel::Sc8850)
				handleKey(g_sc8850Buttons);
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
		if(auto* value = document->GetElementById("bt8850Value"))
		{
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

	void Editor::setPointerButton(const uint32_t _button, const bool _pressed)
	{
		const auto mask = uint32_t{1} << _button;
		if(_pressed) m_pointerButtons |= mask;
		else m_pointerButtons &= ~mask;
		sendButtons();
		updateButtonVisuals();
	}

	void Editor::setKeyboardButton(const uint32_t _button, const bool _pressed)
	{
		const auto mask = uint32_t{1} << _button;
		if(_pressed) m_keyboardButtons |= mask;
		else m_keyboardButtons &= ~mask;
		sendButtons();
		updateButtonVisuals();
	}

	void Editor::sendButtons()
	{
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
		else
			for(const auto& binding : g_sc88Buttons) add(binding);
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
		case emu88Lib::DeviceModel::Sc55Mk2:
			// The only two lamps on this panel, in Sc55Mk2::leds() bit order.
			assign({"btAll", "btMute"});
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
		const auto buttons = m_pointerButtons | m_keyboardButtons;
		for(uint32_t button = 0; button < m_buttonElements.size(); ++button)
			if(m_buttonElements[button])
				m_buttonElements[button]->SetClass("pressed", (buttons & (uint32_t{1} << button)) != 0);
	}

	void Editor::updateLeds(const uint8_t _value)
	{
		for(uint8_t i = 0; i < m_leds.size(); ++i)
			if(m_leds[i])
				m_leds[i]->SetClass("on", (_value & (uint8_t{1} << i)) != 0);
	}

	KeyboardShortcutGroups Editor::buildKeyboardGroups()
	{
		const auto model = m_processor.deviceModel();
		const auto is8850 = model == emu88Lib::DeviceModel::Sc8850;

		const auto keyFor = [is8850](const uint8_t _button) -> std::string
		{
			const auto find = [_button](const auto& _table) -> std::string
			{
				for(const auto& binding : _table)
					if(binding.button == _button)
						return keyName(binding.key);
				return {};
			};
			return is8850 ? find(g_sc8850Buttons) : find(g_sc88Buttons);
		};
		const auto populated = [model, is8850](const uint8_t _button)
		{
			const auto bit = uint32_t{1} << _button;
			return is8850 ? bit != 0 : panelButtonsForDevice(model, bit) != 0;
		};

		KeyboardShortcutGroups groups;
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
		else
			walk(g_sc88Help);

		// A group whose switches this board has none of leaves an empty heading.
		groups.erase(std::remove_if(groups.begin(), groups.end(),
			[](const KeyboardShortcutGroup& _group) { return _group.rows.empty(); }), groups.end());
		return groups;
	}
}
