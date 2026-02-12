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

	MidiLearnTranslator::~MidiLearnTranslator() = default;

	void MidiLearnTranslator::setPreset(const MidiLearnPreset& _preset)
	{
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
}
