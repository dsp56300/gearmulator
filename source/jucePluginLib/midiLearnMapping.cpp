#include "midiLearnMapping.h"

#include "synthLib/midiTypes.h"

#include <juce_core/juce_core.h>

namespace pluginLib
{
	bool MidiLearnMapping::matchesMidiEvent(Type _eventType, uint8_t _eventChannel, uint8_t _eventController) const
	{
		if (type != _eventType)
			return false;

		if (channel != _eventChannel)
			return false;

		// For NRPN, we would need to match the NRPN number, but that's for future implementation
		if (type == Type::NRPN)
			return nrpn == _eventController; // Simplified for now

		// For CC and PolyPressure, match controller number
		if (type == Type::ControlChange || type == Type::PolyPressure)
			return controller == _eventController;

		// For ChannelPressure and PitchBend, controller number is not relevant
		return true;
	}

	bool MidiLearnMapping::matchesMidiEvent(const synthLib::SMidiEvent& _event) const
	{
		const auto statusByte = static_cast<synthLib::MidiStatusByte>(_event.a & 0xf0);
		const auto eventType = midiStatusToType(statusByte);
		const auto eventChannel = getChannel(_event);
		const auto eventController = getController(_event);

		return matchesMidiEvent(eventType, eventChannel, eventController);
	}

	uint8_t MidiLearnMapping::getChannel(const synthLib::SMidiEvent& _event)
	{
		return _event.a & 0x0f;
	}

	uint8_t MidiLearnMapping::getController(const synthLib::SMidiEvent& _event)
	{
		// For CC and PolyPressure, b contains the controller/note number
		return _event.b;
	}

	uint8_t MidiLearnMapping::getValue(const synthLib::SMidiEvent& _event)
	{
		// For most MIDI messages, c contains the value
		return _event.c;
	}

	juce::var MidiLearnMapping::toJson() const
	{
		auto* obj = new juce::DynamicObject();

		obj->setProperty("type", juce::String(typeToString(type)));
		obj->setProperty("mode", juce::String(modeToString(mode)));
		obj->setProperty("channel", static_cast<int>(channel));
		obj->setProperty("controller", static_cast<int>(controller));
		obj->setProperty("nrpn", static_cast<int>(nrpn));
		obj->setProperty("paramName", juce::String(paramName));
		obj->setProperty("feedbackTargets", static_cast<int>(feedbackTargets));

		return juce::var(obj);
	}

	MidiLearnMapping MidiLearnMapping::fromJson(const juce::var& _json)
	{
		MidiLearnMapping mapping;

		if (auto* obj = _json.getDynamicObject())
		{
			mapping.type = stringToType(obj->getProperty("type").toString().toStdString());
			mapping.mode = stringToMode(obj->getProperty("mode").toString().toStdString());
			mapping.channel = static_cast<uint8_t>(static_cast<int>(obj->getProperty("channel")));
			mapping.controller = static_cast<uint8_t>(static_cast<int>(obj->getProperty("controller")));
			mapping.nrpn = static_cast<uint16_t>(static_cast<int>(obj->getProperty("nrpn")));
			mapping.paramName = obj->getProperty("paramName").toString().toStdString();
			mapping.feedbackTargets = static_cast<uint8_t>(static_cast<int>(obj->getProperty("feedbackTargets")));
		}

		return mapping;
	}

	std::string MidiLearnMapping::typeToString(Type _type)
	{
		switch (_type)
		{
		case Type::ControlChange: return "ControlChange";
		case Type::PolyPressure: return "PolyPressure";
		case Type::ChannelPressure: return "ChannelPressure";
		case Type::PitchBend: return "PitchBend";
		case Type::NRPN: return "NRPN";
		default: return "ControlChange";
		}
	}

	MidiLearnMapping::Type MidiLearnMapping::stringToType(const std::string& _str)
	{
		if (_str == "ControlChange") return Type::ControlChange;
		if (_str == "PolyPressure") return Type::PolyPressure;
		if (_str == "ChannelPressure") return Type::ChannelPressure;
		if (_str == "PitchBend") return Type::PitchBend;
		if (_str == "NRPN") return Type::NRPN;
		return Type::ControlChange;
	}

	std::string MidiLearnMapping::modeToString(Mode _mode)
	{
		switch (_mode)
		{
		case Mode::Absolute: return "Absolute";
		case Mode::Relative: return "Relative";
		default: return "Absolute";
		}
	}

	MidiLearnMapping::Mode MidiLearnMapping::stringToMode(const std::string& _str)
	{
		if (_str == "Absolute") return Mode::Absolute;
		if (_str == "Relative") return Mode::Relative;
		return Mode::Absolute;
	}

	MidiLearnMapping::Type MidiLearnMapping::midiStatusToType(synthLib::MidiStatusByte _statusByte)
	{
		switch (_statusByte)
		{
		case synthLib::M_CONTROLCHANGE: return Type::ControlChange;
		case synthLib::M_POLYPRESSURE: return Type::PolyPressure;
		case synthLib::M_AFTERTOUCH: return Type::ChannelPressure;
		case synthLib::M_PITCHBEND: return Type::PitchBend;
		default: return Type::ControlChange;
		}
	}

	synthLib::MidiStatusByte MidiLearnMapping::typeToMidiStatus(Type _type)
	{
		switch (_type)
		{
		case Type::ControlChange: return synthLib::M_CONTROLCHANGE;
		case Type::PolyPressure: return synthLib::M_POLYPRESSURE;
		case Type::ChannelPressure: return synthLib::M_AFTERTOUCH;
		case Type::PitchBend: return synthLib::M_PITCHBEND;
		case Type::NRPN: return synthLib::M_CONTROLCHANGE; // NRPN uses CC messages
		default: return synthLib::M_CONTROLCHANGE;
		}
	}

	synthLib::SMidiEvent MidiLearnMapping::toMidiEvent(const MidiLearnMapping& _mapping)
	{
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Editor);
		
		const auto statusByte = typeToMidiStatus(_mapping.type);
		event.a = statusByte | (_mapping.channel & 0x0f);
		event.b = _mapping.controller;
		event.c = 0;  // Value doesn't matter for display purposes
		
		return event;
	}

	void MidiLearnMapping::setFeedbackEnabled(synthLib::MidiEventSource _target, bool _enabled)
	{
		const auto flag = midiEventSourceToFeedbackTarget(_target);
		if (_enabled)
			feedbackTargets |= flag;
		else
			feedbackTargets &= ~flag;
	}

	bool MidiLearnMapping::isFeedbackEnabled(synthLib::MidiEventSource _target) const
	{
		const auto flag = midiEventSourceToFeedbackTarget(_target);
		return (feedbackTargets & flag) != 0;
	}

	MidiLearnMapping::FeedbackTarget MidiLearnMapping::midiEventSourceToFeedbackTarget(synthLib::MidiEventSource _source)
	{
		switch (_source)
		{
		case synthLib::MidiEventSource::Device: return FeedbackTarget::Device;
		case synthLib::MidiEventSource::Editor: return FeedbackTarget::Editor;
		case synthLib::MidiEventSource::Host: return FeedbackTarget::Host;
		case synthLib::MidiEventSource::Physical: return FeedbackTarget::Physical;
		default: return FeedbackTarget::None;
		}
	}
}
