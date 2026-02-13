#include "midiLearnTranslator.h"

#include "controller.h"
#include "controllermap.h"
#include "synthLib/midiTypes.h"
#include "baseLib/binarystream.h"

#include <juce_data_structures/juce_data_structures.h>

namespace pluginLib
{
	MidiLearnTranslator::MidiLearnTranslator(Controller& _controller, const ControllerMap& _controllerMap)
		: m_controller(_controller)
		, m_controllerMap(_controllerMap)
	{
	}

	MidiLearnTranslator::~MidiLearnTranslator()
	{
		unsubscribeFromParameters();
	}

	void MidiLearnTranslator::setPreset(const MidiLearnPreset& _preset)
	{
		// Unsubscribe from old parameters
		unsubscribeFromParameters();

		m_preset = _preset;

		// Rebuild cache for fast lookup
		m_midiToMappingIndex.clear();

		const auto& mappings = m_preset.getMappings();
		for (size_t i = 0; i < mappings.size(); ++i)
		{
			const auto& mapping = mappings[i];
			
			// Only cache CC and PolyPressure which have controller numbers
			if (mapping.type == MidiLearnMapping::Type::ControlChange ||
			    mapping.type == MidiLearnMapping::Type::PolyPressure)
			{
				const uint32_t key = (static_cast<uint32_t>(mapping.channel) << 8) | mapping.controller;
				m_midiToMappingIndex[key] = i;
			}
		}

		// Subscribe to new parameters for feedback
		subscribeToParameters();
	}

	bool MidiLearnTranslator::processMidiInput(const synthLib::SMidiEvent& _event)
	{
		// If we're in learning mode, handle learning instead of normal processing
		if (m_isLearning)
		{
			handleLearning(_event);
			return true; // Consumed
		}

		// Try to find a learned mapping for this MIDI event FIRST
		// Learned mappings override default controller mappings
		const auto* mapping = m_preset.findMapping(_event);
		if (mapping)
		{
			// Found a learned mapping - apply it and consume the event
			applyMapping(*mapping, _event);
			return true; // Consumed - don't forward to device
		}

		// No learned mapping found
		// Check if this is a default controller mapping handled by firmware
		// If so, don't consume it - let the firmware handle it
		if (isDefaultControllerMapping(_event))
			return false; // Not consumed - forward to device

		// Unknown MIDI event - not learned and not a default mapping
		return false; // Not consumed
	}

	void MidiLearnTranslator::startLearning(const std::string& _paramName)
	{
		m_isLearning = true;
		m_learningParamName = _paramName;
		m_learningValues.clear();
		m_learningChannel = 0;
		m_learningController = 0;
	}

	void MidiLearnTranslator::cancelLearning()
	{
		m_isLearning = false;
		m_learningParamName.clear();
		m_learningValues.clear();
	}

	bool MidiLearnTranslator::isDefaultControllerMapping(const synthLib::SMidiEvent& _event) const
	{
		// Check if this MIDI event matches any default controller mapping
		const auto& params = m_controllerMap.getParameters(_event);
		return !params.empty();
	}

	void MidiLearnTranslator::applyMapping(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event)
	{
		// Get the parameter to update
		auto* param = m_controller.getParameter(_mapping.paramName, 0);
		if (!param)
			return;

		const auto value = MidiLearnMapping::getValue(_event);

		if (_mapping.mode == MidiLearnMapping::Mode::Relative)
		{
			// Relative mode: increment or decrement
			// 0x7F (127) = -1 (decrement), 0x01 (1) = +1 (increment)
			const auto currentValue = param->getUnnormalizedValue();
			const auto& desc = param->getDescription();
			
			int newValue = currentValue;
			if (value == 0x7F) // Decrement
			{
				newValue = std::max(static_cast<int>(desc.range.getStart()), currentValue - 1);
			}
			else if (value == 0x01) // Increment
			{
				newValue = std::min(static_cast<int>(desc.range.getEnd()), currentValue + 1);
			}
			
			param->setUnnormalizedValueNotifyingHost(newValue, Parameter::Origin::Midi);
		}
		else
		{
			// Absolute mode: map MIDI value (0-127) to parameter range
			const auto& desc = param->getDescription();
			const auto rangeSize = desc.range.getEnd() - desc.range.getStart();
			const auto normalized = value / 127.0f;
			const auto paramValue = static_cast<int>(desc.range.getStart() + normalized * rangeSize);
			
			param->setUnnormalizedValueNotifyingHost(paramValue, Parameter::Origin::Midi);
		}
	}

	void MidiLearnTranslator::handleLearning(const synthLib::SMidiEvent& _event)
	{
		// Check if this input source is enabled for learning
		const uint8_t sourceBit = static_cast<uint8_t>(_event.source);
		if (!(m_learnInputSources & sourceBit))
			return; // Ignore events from disabled sources

		// Only support CC learning for now (Phase 1)
		const auto statusByte = static_cast<synthLib::MidiStatusByte>(_event.a & 0xf0);
		if (statusByte != synthLib::M_CONTROLCHANGE)
			return;

		const auto channel = MidiLearnMapping::getChannel(_event);
		const auto controller = MidiLearnMapping::getController(_event);
		const auto value = MidiLearnMapping::getValue(_event);

		// First event: establish channel and controller
		if (m_learningValues.empty())
		{
			m_learningChannel = channel;
			m_learningController = controller;
			m_learningValues.push_back(value);

			// Notify progress
			if (onLearningProgress)
				onLearningProgress(1, kRequiredUniqueValues);
			return;
		}

		// Subsequent events: must be same channel and controller
		if (channel != m_learningChannel || controller != m_learningController)
			return; // Ignore events from different CC

		// Only add if it's a unique value
		if (std::find(m_learningValues.begin(), m_learningValues.end(), value) == m_learningValues.end())
		{
			m_learningValues.push_back(value);
		}

		// Count unique values
		const size_t uniqueCount = m_learningValues.size();

		// Notify progress
		if (onLearningProgress)
			onLearningProgress(uniqueCount, kRequiredUniqueValues);

		// Wait for enough unique values (forces user to rotate in both directions)
		if (uniqueCount < kRequiredUniqueValues)
			return;

		// We have enough unique values, create the mapping
		MidiLearnMapping newMapping;
		newMapping.type = MidiLearnMapping::midiStatusToType(statusByte);
		newMapping.channel = m_learningChannel;
		newMapping.controller = m_learningController;
		newMapping.paramName = m_learningParamName;
		newMapping.mode = detectMode(m_learningValues);

		// Check if this MIDI controller (channel+CC) is already mapped to a different parameter
		const auto* existingMapping = m_preset.findMapping(newMapping.type, newMapping.channel, newMapping.controller);
		if (existingMapping && existingMapping->paramName != m_learningParamName)
		{
			// MIDI controller is mapped to a different parameter - show conflict
			if (onMappingConflict)
				onMappingConflict(newMapping);
		}
		else
		{
			// No conflict, notify success
			if (onMappingLearned)
				onMappingLearned(newMapping);
		}

		// Exit learning mode
		cancelLearning();
	}

	MidiLearnMapping::Mode MidiLearnTranslator::detectMode(const std::vector<uint8_t>& _values) const
	{
		if (_values.empty())
			return MidiLearnMapping::Mode::Absolute;

		// Check for relative encoder patterns:
		// - Two's complement: 0x3F-0x01 for negative, 0x41-0x7F for positive (center at 0x40)
		// - Sign bit: 0x01-0x7F for positive, 0x7F-0x01 for negative
		// Relative encoders typically send values like 0x3F, 0x41, or 0x01, 0x7F

		bool hasLowValues = false;  // Values near 0x01
		bool hasHighValues = false; // Values near 0x7F
		bool hasMidValues = false;  // Values around 0x3F-0x41

		for (const auto value : _values)
		{
			if (value <= 0x02) // 0x00, 0x01, 0x02
				hasLowValues = true;
			else if (value >= 0x7D) // 0x7D, 0x7E, 0x7F
				hasHighValues = true;
			else if (value >= 0x3E && value <= 0x42) // Around 0x40 (center)
				hasMidValues = true;
		}

		// If we see both very low and very high values, or values clustered around center,
		// it's likely a relative encoder
		if ((hasLowValues && hasHighValues) || hasMidValues)
			return MidiLearnMapping::Mode::Relative;

		// Check if values are changing sequentially (absolute fader/knob)
		// Calculate variance - absolute controls show gradual changes
		if (_values.size() >= 2)
		{
			bool isSequential = true;
			for (size_t i = 1; i < _values.size(); ++i)
			{
				const int diff = std::abs(static_cast<int>(_values[i]) - static_cast<int>(_values[i - 1]));
				// Absolute controls typically change by small amounts (1-10)
				// Relative encoders can jump wildly
				if (diff > 20)
				{
					isSequential = false;
					break;
				}
			}

			if (isSequential)
				return MidiLearnMapping::Mode::Absolute;
		}

		// Default to absolute if unsure
		return MidiLearnMapping::Mode::Absolute;
	}

	void MidiLearnTranslator::subscribeToParameters()
	{
		const auto& mappings = m_preset.getMappings();
		m_paramListenerIds.reserve(mappings.size());

		for (const auto& mapping : mappings)
		{
			// Only subscribe if feedback is enabled for at least one target
			if (mapping.feedbackTargets == 0)
				continue;

			auto* param = m_controller.getParameter(mapping.paramName, 0);
			if (!param)
				continue;

			// Subscribe to parameter changes
			const auto listenerId = param->onValueChanged.addListener(
				[this, paramName = mapping.paramName](Parameter* _param)
				{
					// Don't send feedback for MIDI-originated changes (avoid feedback loops)
					if (_param->getChangeOrigin() == Parameter::Origin::Midi)
						return;

					onParameterChanged(paramName, _param->getValue());
				});

			m_paramListenerIds.push_back(listenerId);
		}
	}

	void MidiLearnTranslator::unsubscribeFromParameters()
	{
		const auto& mappings = m_preset.getMappings();
		
		// Remove listeners
		for (size_t i = 0; i < m_paramListenerIds.size() && i < mappings.size(); ++i)
		{
			auto* param = m_controller.getParameter(mappings[i].paramName, 0);
			if (param)
				param->onValueChanged.removeListener(m_paramListenerIds[i]);
		}

		m_paramListenerIds.clear();
	}

	void MidiLearnTranslator::onParameterChanged(const std::string& _paramName, float _normalizedValue)
	{
		// Find all mappings for this parameter
		const auto mappings = m_preset.findMappingsByParam(_paramName);
		
		for (const auto* mapping : mappings)
		{
			if (!mapping || mapping->feedbackTargets == 0)
				continue;

			// Create the feedback MIDI event
			const auto feedbackEvent = createFeedbackEvent(*mapping, _normalizedValue);

			// Send to each enabled feedback target
			if (!onSendMidiOutput)
				continue;

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Device))
				onSendMidiOutput(synthLib::MidiEventSource::Device, feedbackEvent);

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Editor))
				onSendMidiOutput(synthLib::MidiEventSource::Editor, feedbackEvent);

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Host))
				onSendMidiOutput(synthLib::MidiEventSource::Host, feedbackEvent);

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Physical))
				onSendMidiOutput(synthLib::MidiEventSource::Physical, feedbackEvent);
		}
	}

	synthLib::SMidiEvent MidiLearnTranslator::createFeedbackEvent(const MidiLearnMapping& _mapping, float _normalizedValue) const
	{
		// Always send absolute values for feedback (even for relative-mode mappings)
		const uint8_t midiValue = static_cast<uint8_t>(std::clamp(_normalizedValue * 127.0f, 0.0f, 127.0f));

		// Build status byte: message type + channel
		const auto statusByte = MidiLearnMapping::typeToMidiStatus(_mapping.type);
		const uint8_t statusWithChannel = statusByte | (_mapping.channel & 0x0f);

		// Create the event based on message type
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Internal);
		event.a = statusWithChannel;

		switch (_mapping.type)
		{
		case MidiLearnMapping::Type::ControlChange:
		case MidiLearnMapping::Type::PolyPressure:
			event.b = _mapping.controller;
			event.c = midiValue;
			break;

		case MidiLearnMapping::Type::ChannelPressure:
			event.b = midiValue;
			event.c = 0;
			break;

		case MidiLearnMapping::Type::PitchBend:
			// Pitch bend uses 14-bit value (LSB, MSB)
			{
				const uint16_t pitchValue = static_cast<uint16_t>(_normalizedValue * 16383.0f);
				event.b = pitchValue & 0x7f; // LSB
				event.c = (pitchValue >> 7) & 0x7f; // MSB
			}
			break;

		case MidiLearnMapping::Type::NRPN:
			// NRPN feedback not yet implemented (needs multiple CC messages)
			break;
		}

		return event;
	}

	void MidiLearnTranslator::saveChunkData(baseLib::BinaryStream& _stream) const
	{
		baseLib::ChunkWriter cw(_stream, "MDLN", 2);

		// Serialize preset to JSON
		const auto json = m_preset.toJson();
		const auto jsonString = juce::JSON::toString(json);
		_stream.write(jsonString.toStdString());
		
		// Save learn input sources
		_stream.write(m_learnInputSources);
	}

	void MidiLearnTranslator::loadChunkData(baseLib::ChunkReader& _cr)
	{
		// Version 1: Only preset JSON
		_cr.add("MDLN", 1, [this](baseLib::BinaryStream& _stream, uint32_t _version)
		{
			const auto jsonString = _stream.readString();
			
			// Parse JSON and load preset
			juce::var json;
			const auto result = juce::JSON::parse(juce::String(jsonString), json);
			
			if (result.wasOk())
			{
				MidiLearnPreset preset;
				if (preset.fromJson(json))
				{
					setPreset(preset);
				}
			}
		});
		
		// Version 2: Preset JSON + input sources
		_cr.add("MDLN", 2, [this](baseLib::BinaryStream& _stream, uint32_t _version)
		{
			const auto jsonString = _stream.readString();
			
			// Parse JSON and load preset
			juce::var json;
			const auto result = juce::JSON::parse(juce::String(jsonString), json);
			
			if (result.wasOk())
			{
				MidiLearnPreset preset;
				if (preset.fromJson(json))
				{
					setPreset(preset);
				}
			}
			
			// Load learn input sources
			_stream.read(m_learnInputSources);
		});
	}
}
