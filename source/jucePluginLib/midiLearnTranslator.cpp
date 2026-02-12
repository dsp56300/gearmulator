#include "midiLearnTranslator.h"

#include "controller.h"
#include "controllermap.h"
#include "synthLib/midiTypes.h"

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
	}

	void MidiLearnTranslator::cancelLearning()
	{
		m_isLearning = false;
		m_learningParamName.clear();
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
		// Only support CC learning for now (Phase 1)
		const auto statusByte = static_cast<synthLib::MidiStatusByte>(_event.a & 0xf0);
		if (statusByte != synthLib::M_CONTROLCHANGE)
			return;

		// Create a new mapping
		MidiLearnMapping newMapping;
		newMapping.type = MidiLearnMapping::midiStatusToType(statusByte);
		newMapping.channel = MidiLearnMapping::getChannel(_event);
		newMapping.controller = MidiLearnMapping::getController(_event);
		newMapping.paramName = m_learningParamName;
		newMapping.mode = detectMode(MidiLearnMapping::getValue(_event));

		// Check if this parameter already has a mapping
		const auto existingMappings = m_preset.findMappingsByParam(m_learningParamName);
		if (!existingMappings.empty())
		{
			// Notify about conflict
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

	MidiLearnMapping::Mode MidiLearnTranslator::detectMode(uint8_t _value) const
	{
		// Auto-detect relative mode: if value is 0x7F (127) or 0x01 (1), assume relative
		if (_value == 0x7F || _value == 0x01)
			return MidiLearnMapping::Mode::Relative;
		
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
}
