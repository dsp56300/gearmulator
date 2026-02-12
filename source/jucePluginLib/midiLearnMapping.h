#pragma once

#include <juce_core/juce_core.h>
#include <string>
#include <cstdint>

namespace synthLib { struct SMidiEvent; enum MidiStatusByte; enum class MidiEventSource : uint8_t; }

namespace pluginLib
{
	struct MidiLearnMapping
	{
		enum class Type
		{
			ControlChange,
			PolyPressure,
			ChannelPressure,
			PitchBend,
			NRPN
		};

		enum class Mode
		{
			Absolute,
			Relative,
			Count
		};
		
		static constexpr const char* ModeStrings[] = { "Absolute", "Relative" };
		static_assert(static_cast<size_t>(Mode::Count) == sizeof(ModeStrings) / sizeof(ModeStrings[0]), 
		              "ModeStrings array size must match Mode enum count");

		// Feedback target flags (bitmask)
		enum FeedbackTarget : uint8_t
		{
			None = 0x00,
			Device = 0x01,
			Editor = 0x02,
			Host = 0x04,
			Physical = 0x08,
			All = 0x0F
		};

		Type type = Type::ControlChange;
		Mode mode = Mode::Absolute;
		uint8_t channel = 0;         // MIDI channel (0-15, displayed as 1-16)
		uint8_t controller = 0;      // CC number (0-127) or PP note number
		uint16_t nrpn = 0;           // For NRPN type
		std::string paramName;       // Target parameter name
		uint8_t feedbackTargets = 0; // Bitmask of FeedbackTarget flags

		// Comparison for finding mappings
		bool matchesMidiEvent(Type _eventType, uint8_t _eventChannel, uint8_t _eventController) const;
		bool matchesMidiEvent(const synthLib::SMidiEvent& _event) const;

		// Extract values from MIDI event
		static uint8_t getChannel(const synthLib::SMidiEvent& _event);
		static uint8_t getController(const synthLib::SMidiEvent& _event);
		static uint8_t getValue(const synthLib::SMidiEvent& _event);

		// JSON serialization
		juce::var toJson() const;
		static MidiLearnMapping fromJson(const juce::var& _json);

		// Helpers for type/mode strings
		static std::string typeToString(Type _type);
		static Type stringToType(const std::string& _str);
		static std::string modeToString(Mode _mode);
		static Mode stringToMode(const std::string& _str);

		// Convert between MidiStatusByte and Type
		static Type midiStatusToType(synthLib::MidiStatusByte _statusByte);
		static synthLib::MidiStatusByte typeToMidiStatus(Type _type);
		
		// Convert to SMidiEvent for feedback/display
		static synthLib::SMidiEvent toMidiEvent(const MidiLearnMapping& _mapping);

		// Feedback target management
		void setFeedbackEnabled(synthLib::MidiEventSource _target, bool _enabled);
		bool isFeedbackEnabled(synthLib::MidiEventSource _target) const;
		static FeedbackTarget midiEventSourceToFeedbackTarget(synthLib::MidiEventSource _source);
	};
}
