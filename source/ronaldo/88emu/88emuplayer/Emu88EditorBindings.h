#pragma once

#include "88lib/sc88types.h"
#include "88lib/sc88pro.h"
#include "88lib/sc8850.h"
#include "RmlUi/Core/Input.h"
#include <array>
#include <string>

namespace emu88Player::editor
{
	constexpr int g_defaultWidth = 1040;
	constexpr int g_defaultHeight = 318;
	// The project-wide links, same as every other plugin's skin carries.
	constexpr auto g_websiteUrl = "https://dsp56300.wordpress.com";
	constexpr auto g_donateUrl = "https://paypal.me/dsp56300";
	constexpr int g_aboutWidth = 420;
	constexpr int g_keyboardWidth = 460;
	// Switches sharing a panel row differ by at most 3dp; the next row is 26dp
	// away. Expressed in dp because element offsets come back in rendered
	// pixels, which move with the GUI scale.
	constexpr float g_keyboardRowToleranceDp = 8.0f;
	constexpr int g_aboutHeight = 242;
	constexpr int g_settingsWidth = 560;
	constexpr int g_settingsHeight = 360;
	constexpr int g_volumeMinimum = 0;
	// The standalone shell puts its own Options button at (8, 6, 60, h-8);
	// the recorder button follows it along the same baseline.
	constexpr int g_titleBarButtonY = 6;
	constexpr int g_optionsButtonRight = 8 + 60;
	constexpr int g_titleBarButtonGap = 6;
	constexpr int g_recordIconMargin = 7;
	constexpr int g_recordIconGap = 5;
	constexpr uint32_t g_recordIconColour = 0xffe03c3c;
	constexpr auto g_recordLabel = "Record to WAV";
	constexpr auto g_recordStopLabel = "Stop and Save";
	constexpr int g_volumeUnity = 100;
	constexpr int g_volumeMaximum = 200;
	constexpr std::array g_guiScales{50, 65, 75, 85, 100, 125, 150, 175, 200, 250, 300};
	// A VALUE click pushes the encoder switch for this long; a pointer that
	// travelled further than this radius between press and release was a
	// turn, not a push.
	constexpr int kValuePushPulseMs = 80;
	constexpr float kValuePushClickRadius = 3.0f;

	using Key = Rml::Input::KeyIdentifier;
	// label is the panel legend, for the keyboard help dialog; it matches the
	// skin's title attribute, and covers the switches that have no face here.
	struct ButtonBinding { const char* element; uint8_t button; Key key; const char* label; };
	template<typename Button>
	constexpr ButtonBinding bind(const char* _element, const Button _button, const Key _key,
	                             const char* _label)
	{
		return {_element, static_cast<uint8_t>(_button), _key, _label};
	}

	constexpr std::array g_sc88Buttons{
		bind(nullptr, emu88Lib::Sc88ProButton::Power, Key::KI_Q, "POWER"),
		bind("btAll", emu88Lib::Sc88ProButton::InstAll, Key::KI_W, "ALL"),
		bind("btMute", emu88Lib::Sc88ProButton::InstMute, Key::KI_E, "MUTE"),
		bind("btSc55Map", emu88Lib::Sc88ProButton::Sc55Map, Key::KI_1, "SC-55 MAP"),
		bind("btSc88Map", emu88Lib::Sc88ProButton::Sc88Map, Key::KI_2, "SC-88 MAP"),
		bind("btUserInst", emu88Lib::Sc88ProButton::UserInst, Key::KI_3, "USER INST"),
		bind("btSelect", emu88Lib::Sc88ProButton::Select, Key::KI_4, "SELECT"),
		bind("btPreview", emu88Lib::Sc88ProButton::Preview, Key::KI_TAB, "PREVIEW"),
		bind("btPartL", emu88Lib::Sc88ProButton::PartL, Key::KI_R, "PART left"),
		bind("btPartR", emu88Lib::Sc88ProButton::PartR, Key::KI_T, "PART right"),
		bind("btInstL", emu88Lib::Sc88ProButton::InstL, Key::KI_Y, "INSTRUMENT left"),
		bind("btInstR", emu88Lib::Sc88ProButton::InstR, Key::KI_U, "INSTRUMENT right"),
		bind("btKeyShiftL", emu88Lib::Sc88ProButton::KeyShiftL, Key::KI_I, "KEY SHIFT left"),
		bind("btKeyShiftR", emu88Lib::Sc88ProButton::KeyShiftR, Key::KI_O, "KEY SHIFT right"),
		bind("btLevelL", emu88Lib::Sc88ProButton::LevelL, Key::KI_P, "LEVEL left"),
		bind("btLevelR", emu88Lib::Sc88ProButton::LevelR, Key::KI_OEM_4, "LEVEL right"),
		bind("btMidiChL", emu88Lib::Sc88ProButton::MidiChL, Key::KI_A, "MIDI CH left"),
		bind("btMidiChR", emu88Lib::Sc88ProButton::MidiChR, Key::KI_S, "MIDI CH right"),
		bind("btPanL", emu88Lib::Sc88ProButton::PanL, Key::KI_D, "PAN left"),
		bind("btPanR", emu88Lib::Sc88ProButton::PanR, Key::KI_F, "PAN right"),
		bind("btReverbL", emu88Lib::Sc88ProButton::ReverbL, Key::KI_G, "REVERB left"),
		bind("btReverbR", emu88Lib::Sc88ProButton::ReverbR, Key::KI_H, "REVERB right"),
		bind("btChorusL", emu88Lib::Sc88ProButton::ChorusL, Key::KI_J, "CHORUS left"),
		bind("btChorusR", emu88Lib::Sc88ProButton::ChorusR, Key::KI_K, "CHORUS right"),
		bind("btVibRateL", emu88Lib::Sc88ProButton::VibRateL, Key::KI_Z, "VIB RATE / EFX TYPE left"),
		bind("btVibRateR", emu88Lib::Sc88ProButton::VibRateR, Key::KI_X, "VIB RATE / EFX TYPE right"),
		bind("btVibDepthL", emu88Lib::Sc88ProButton::VibDepthL, Key::KI_C, "VIB DEPTH / EFX PARAM left"),
		bind("btVibDepthR", emu88Lib::Sc88ProButton::VibDepthR, Key::KI_V, "VIB DEPTH / EFX PARAM right"),
		bind("btVibDelayL", emu88Lib::Sc88ProButton::VibDelayL, Key::KI_B, "VIB DELAY / EFX VALUE left"),
		bind("btVibDelayR", emu88Lib::Sc88ProButton::VibDelayR, Key::KI_N, "VIB DELAY / EFX VALUE right"),
	};

	constexpr std::array g_sc8850Buttons{
		bind("bt8850Edit", emu88Lib::Sc8850Button::Edit, Key::KI_E, "EDIT"),
		bind("bt8850PartL", emu88Lib::Sc8850Button::PartLeft, Key::KI_LEFT, "PART left"),
		bind("bt8850PartR", emu88Lib::Sc8850Button::PartRight, Key::KI_RIGHT, "PART right"),
		bind("bt8850Drum", emu88Lib::Sc8850Button::Drum, Key::KI_D, "DRUM"),
		bind("bt8850Variation", emu88Lib::Sc8850Button::Variation, Key::KI_V, "VARIATION"),
		bind("bt8850Instrument", emu88Lib::Sc8850Button::Instrument, Key::KI_N, "INSTRUMENT"),
		bind("bt8850Effects", emu88Lib::Sc8850Button::Effects, Key::KI_X, "EFFECTS"),
		bind("bt8850Exit", emu88Lib::Sc8850Button::Exit, Key::KI_BACK, "EXIT"),
		bind("bt8850Enter", emu88Lib::Sc8850Button::Enter, Key::KI_RETURN, "ENTER"),
		bind("bt8850Shift", emu88Lib::Sc8850Button::Shift, Key::KI_LSHIFT, "SHIFT"),
		bind("bt8850Solo", emu88Lib::Sc8850Button::Solo, Key::KI_S, "SOLO"),
		bind("bt8850Mute", emu88Lib::Sc8850Button::Mute, Key::KI_M, "MUTE"),
		bind("bt8850Dec", emu88Lib::Sc8850Button::Dec, Key::KI_OEM_MINUS, "DEC"),
		bind("bt8850Inc", emu88Lib::Sc8850Button::Inc, Key::KI_OEM_PLUS, "INC"),
		bind("bt8850F1", emu88Lib::Sc8850Button::F1, Key::KI_F1, "F1"),
		bind("bt8850F2", emu88Lib::Sc8850Button::F2, Key::KI_F2, "F2"),
		bind("bt8850F3", emu88Lib::Sc8850Button::F3, Key::KI_F3, "F3"),
		bind("bt8850F4", emu88Lib::Sc8850Button::F4, Key::KI_F4, "F4"),
		bind("bt8850InstMap", emu88Lib::Sc8850Button::InstMap, Key::KI_I, "INST MAP"),
		bind("bt8850Value", emu88Lib::Sc8850Button::ValuePush, Key::KI_SPACE, "VALUE push"),
		bind("bt8850Preview", emu88Lib::Sc8850Button::PreviewPush, Key::KI_P, "PREVIEW"),
	};

	// The bindings the help dialog has to spell out; RmlUi's identifiers are
	// contiguous for the letter, digit and function-key runs.
	inline std::string keyName(const Key _key)
	{
		switch(_key)
		{
		case Key::KI_TAB:       return "Tab";
		case Key::KI_BACK:      return "Backspace";
		case Key::KI_RETURN:    return "Return";
		case Key::KI_LSHIFT:    return "Left shift";
		case Key::KI_SPACE:     return "Space";
		case Key::KI_LEFT:      return "\xe2\x86\x90";	// left arrow
		case Key::KI_RIGHT:     return "\xe2\x86\x92";	// right arrow
		case Key::KI_OEM_1:     return ";";
		case Key::KI_OEM_4:     return "[";
		case Key::KI_OEM_MINUS: return "-";
		case Key::KI_OEM_PLUS:  return "+";
		default: break;
		}
		if(_key >= Key::KI_A && _key <= Key::KI_Z)
			return std::string(1, static_cast<char>('A' + (_key - Key::KI_A)));
		if(_key >= Key::KI_0 && _key <= Key::KI_9)
			return std::string(1, static_cast<char>('0' + (_key - Key::KI_0)));
		if(_key >= Key::KI_F1 && _key <= Key::KI_F4)
			return "F" + std::to_string(1 + (_key - Key::KI_F1));
		return {};
	}

	constexpr uint32_t proButtonBit(const emu88Lib::Sc88ProButton _button)
	{
		return uint32_t{1} << static_cast<uint8_t>(_button);
	}

	// The SC-55mk2 shares the family's switch-matrix numbering but populates
	// only the SC-55 positions: no map/EQ, PREVIEW, USER INST/SELECT or VIB row.
	constexpr uint32_t g_sc55Buttons =
		emu88Lib::buttonBit(emu88Lib::Button::InstL) | emu88Lib::buttonBit(emu88Lib::Button::InstR) |
		emu88Lib::buttonBit(emu88Lib::Button::InstMute) | emu88Lib::buttonBit(emu88Lib::Button::InstAll) |
		emu88Lib::buttonBit(emu88Lib::Button::MidiChL) | emu88Lib::buttonBit(emu88Lib::Button::MidiChR) |
		emu88Lib::buttonBit(emu88Lib::Button::ChorusL) | emu88Lib::buttonBit(emu88Lib::Button::ChorusR) |
		emu88Lib::buttonBit(emu88Lib::Button::PanL) | emu88Lib::buttonBit(emu88Lib::Button::PanR) |
		emu88Lib::buttonBit(emu88Lib::Button::PartL) | emu88Lib::buttonBit(emu88Lib::Button::PartR) |
		emu88Lib::buttonBit(emu88Lib::Button::KeyShiftL) | emu88Lib::buttonBit(emu88Lib::Button::KeyShiftR) |
		emu88Lib::buttonBit(emu88Lib::Button::ReverbL) | emu88Lib::buttonBit(emu88Lib::Button::ReverbR) |
		emu88Lib::buttonBit(emu88Lib::Button::LevelL) | emu88Lib::buttonBit(emu88Lib::Button::LevelR);

	constexpr uint32_t panelButtonsForDevice(const emu88Lib::DeviceModel _model, uint32_t _buttons)
	{
		constexpr auto preview = proButtonBit(emu88Lib::Sc88ProButton::Preview);
		if(_model == emu88Lib::DeviceModel::Sc88VL)
			return _buttons & ~preview;
		if(_model == emu88Lib::DeviceModel::Sc55Mk2)
			return _buttons & g_sc55Buttons;
		return _buttons;
	}

	static_assert(panelButtonsForDevice(emu88Lib::DeviceModel::Sc88VL,
	                                    proButtonBit(emu88Lib::Sc88ProButton::Preview)) == 0);
	static_assert(panelButtonsForDevice(emu88Lib::DeviceModel::Sc55Mk2,
	                                    proButtonBit(emu88Lib::Sc88ProButton::Sc55Map) |
	                                    proButtonBit(emu88Lib::Sc88ProButton::UserInst) |
	                                    proButtonBit(emu88Lib::Sc88ProButton::InstAll)) ==
	              proButtonBit(emu88Lib::Sc88ProButton::InstAll));
	// How the keyboard dialog is laid out. Hardcoded rather than read back
	// from the skin, so the rows read the way the front panel is printed:
	// the mode column, then the parameter rockers in panel order with each
	// +/- pair on one line, then the edit row. `second` is kHelpNone for a
	// switch that is not half of a pair; a non-null `group` starts a group.
	constexpr uint8_t kHelpNone = 0xff;
	struct HelpRow
	{
		const char* group; uint8_t first; uint8_t second; const char* label;
		const char* keys;		// non-null: a chord, spelled out rather than looked up
		bool proOnly;
	};

	template<typename Button>
	constexpr HelpRow row(const char* _group, const Button _first, const char* _label)
	{
		return {_group, static_cast<uint8_t>(_first), kHelpNone, _label, nullptr, false};
	}
	template<typename Button>
	constexpr HelpRow pair(const char* _group, const Button _left, const Button _right,
	                       const char* _label)
	{
		return {_group, static_cast<uint8_t>(_left), static_cast<uint8_t>(_right), _label, nullptr, false};
	}
	// A function with no switch of its own: the keys are spelled out, and
	// `_gate` is a switch the board must have for the chord to exist.
	template<typename Button>
	constexpr HelpRow chord(const char* _group, const char* _keys, const Button _gate,
	                        const char* _label, const bool _proOnly)
	{
		return {_group, static_cast<uint8_t>(_gate), kHelpNone, _label, _keys, _proOnly};
	}

	using ProButton = emu88Lib::Sc88ProButton;
	constexpr std::array g_sc88Help{
		row ("Mode",  ProButton::InstAll,  "ALL"),
		row (nullptr, ProButton::InstMute, "MUTE"),
		row (nullptr, ProButton::Sc55Map,  "SC-55 MAP"),
		row (nullptr, ProButton::Sc88Map,  "SC-88 MAP / EQ"),

		pair("Parameters", ProButton::PartL,     ProButton::PartR,     "PART"),
		pair(nullptr, ProButton::InstL,      ProButton::InstR,      "INSTRUMENT"),
		pair(nullptr, ProButton::LevelL,     ProButton::LevelR,     "LEVEL"),
		pair(nullptr, ProButton::PanL,       ProButton::PanR,       "PAN"),
		pair(nullptr, ProButton::ReverbL,    ProButton::ReverbR,    "REVERB"),
		pair(nullptr, ProButton::ChorusL,    ProButton::ChorusR,    "CHORUS"),
		pair(nullptr, ProButton::KeyShiftL,  ProButton::KeyShiftR,  "KEY SHIFT"),
		pair(nullptr, ProButton::MidiChL,    ProButton::MidiChR,    "MIDI CH"),
		// The panel prints DELAY under KEY SHIFT because that is what it is:
		// the same rockers with SC-88 MAP held. There is no delay switch -
		// matrix positions 15 and 23 do nothing when pressed.
		chord("Chords", "SC-88 MAP + KEY SHIFT", ProButton::Sc88Map, "DELAY", true),

		// The bottom three pairs are mode-dependent - USER INST and SELECT
		// move them between vibrato, filter, envelope and EFX. Labelled by
		// their EFX legend; the panel prints the rest above them.
		row ("Edit",  ProButton::Preview,  "PREVIEW"),
		row (nullptr, ProButton::UserInst, "USER INST"),
		row (nullptr, ProButton::Select,   "SELECT"),
		pair(nullptr, ProButton::VibRateL,  ProButton::VibRateR,  "EFX TYPE"),
		pair(nullptr, ProButton::VibDepthL, ProButton::VibDepthR, "EFX PARAM"),
		pair(nullptr, ProButton::VibDelayL, ProButton::VibDelayR, "EFX VALUE"),

		row ("Power", ProButton::Power, "POWER"),
	};

	using WideButton = emu88Lib::Sc8850Button;
	constexpr std::array g_sc8850Help{
		row ("Mode",  WideButton::Edit,        "EDIT"),
		row (nullptr, WideButton::Drum,        "DRUM"),
		row (nullptr, WideButton::Effects,     "EFFECTS"),

		pair("Parameters", WideButton::PartLeft, WideButton::PartRight, "PART"),
		row (nullptr, WideButton::Variation,   "VARIATION"),
		row (nullptr, WideButton::Instrument,  "INSTRUMENT"),
		row (nullptr, WideButton::InstMap,     "INST MAP"),
		pair(nullptr, WideButton::Dec,         WideButton::Inc, "DEC / INC"),
		row (nullptr, WideButton::ValuePush,   "VALUE push"),

		row ("Edit",  WideButton::Exit,        "EXIT"),
		row (nullptr, WideButton::Enter,       "ENTER"),
		row (nullptr, WideButton::Shift,       "SHIFT"),
		row (nullptr, WideButton::Solo,        "SOLO"),
		row (nullptr, WideButton::Mute,        "MUTE"),
		row (nullptr, WideButton::PreviewPush, "PREVIEW"),

		row ("Function", WideButton::F1, "F1"),
		row (nullptr, WideButton::F2, "F2"),
		row (nullptr, WideButton::F3, "F3"),
		row (nullptr, WideButton::F4, "F4"),
	};

	static_assert(panelButtonsForDevice(emu88Lib::DeviceModel::Sc8850,
	                                    emu88Lib::sc8850ButtonBit(emu88Lib::Sc8850Button::PreviewPush)) ==
	              emu88Lib::sc8850ButtonBit(emu88Lib::Sc8850Button::PreviewPush));

	// Binary data carries bare filenames, or at most a relative path. Feeding one to
	// juce::File to strip the directory asserts on every entry ("Illegal absolute path")
	// and buries the assertions that matter, so cut at the separator ourselves.
	inline std::string binaryResourceFileName(const char* _path)
	{
		std::string name(_path);
		const auto pos = name.find_last_of("/\\");
		return pos == std::string::npos ? name : name.substr(pos + 1);
	}

	inline const char* findBinaryResource(const std::string& _name, uint32_t& _dataSize,
	                               const int _count, const char* const* _filenames,
	                               const char* const* _resourceNames,
	                               const char* (*_getResource)(const char*, int&))
	{
		for(int i = 0; i < _count; ++i)
		{
			if(binaryResourceFileName(_filenames[i]) != _name)
				continue;
			int size = 0;
			const auto* result = _getResource(_resourceNames[i], size);
			_dataSize = static_cast<uint32_t>(size);
			return result;
		}
		return nullptr;
	}
}
