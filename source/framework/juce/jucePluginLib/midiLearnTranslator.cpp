#include "midiLearnTranslator.h"

#include "controller.h"
#include "controllermap.h"
#include "synthLib/midiTypes.h"
#include "baseLib/binarystream.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace pluginLib
{
	namespace
	{
		// without a message manager, in a test, the caller is as good as the message thread
		bool isMessageThread()
		{
			const auto* mm = juce::MessageManager::getInstanceWithoutCreating();
			return !mm || mm->isThisTheMessageThread();
		}
	}

	MidiLearnTranslator::MidiLearnTranslator(Controller& _controller, const ControllerMap& _controllerMap)
		: m_controller(_controller)
		, m_controllerMap(_controllerMap)
		, m_activePreset(std::make_shared<const MidiLearnPreset>())
	{
	}

	MidiLearnTranslator::~MidiLearnTranslator()
	{
		unsubscribeFromParameters();
	}

	void MidiLearnTranslator::setPreset(const MidiLearnPreset& _preset)
	{
		// a copy first: the settings page passes the message thread copy itself
		auto preset = std::make_shared<const MidiLearnPreset>(_preset);

		{
			std::lock_guard lock(m_mutex);
			m_activePreset = preset;
		}

		if (isMessageThread())
		{
			applyPreset(*preset);
			return;
		}

		// a state restore on the host's thread
		juce::MessageManager::callAsync([this, alive = std::weak_ptr(m_alive), preset]
		{
			if (alive.expired())
				return;
			{
				// a later preset has replaced this one, its own call applies it
				std::lock_guard lock(m_mutex);
				if (m_activePreset != preset)
					return;
			}
			applyPreset(*preset);
		});
	}

	void MidiLearnTranslator::applyPreset(const MidiLearnPreset& _preset)
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
				if (mapping.channel == MidiLearnMapping::AllChannels)
				{
					// AllChannels: populate cache for all 16 channels
					for (uint32_t ch = 0; ch < 16; ++ch)
					{
						const uint32_t key = (ch << 8) | mapping.controller;
						m_midiToMappingIndex[key] = i;
					}
				}
				else
				{
					const uint32_t key = (static_cast<uint32_t>(mapping.channel) << 8) | mapping.controller;
					m_midiToMappingIndex[key] = i;
				}
			}
		}

		// Subscribe to new parameters for feedback
		subscribeToParameters();

		std::lock_guard lock(m_callbackMutex);
		if (m_onPresetChanged)
			m_onPresetChanged();
	}

	bool MidiLearnTranslator::processMidiInput(const synthLib::SMidiEvent& _event)
	{
		// Filter by input source - ignore events from disabled sources
		const uint8_t sourceBit = 1 << static_cast<uint8_t>(_event.source);
		if (!(m_learnInputSources & sourceBit))
			return false; // Not consumed - source is disabled for MIDI Learn

		std::shared_ptr<const MidiLearnPreset> preset;
		std::optional<MidiLearnMapping> learned;
		size_t progress = 0;
		bool learning;

		{
			std::lock_guard lock(m_mutex);
			learning = m_isLearning;
			if (learning)
				progress = handleLearning(_event, learned);
			else
				preset = m_activePreset;
		}

		// If we're in learning mode, handle learning instead of normal processing
		if (learning)
		{
			// outside m_mutex, a callback may well start or cancel learning
			std::lock_guard lock(m_callbackMutex);
			if (progress && m_onLearningProgress)
				m_onLearningProgress(progress, kRequiredUniqueValues);
			if (learned && m_onMappingLearned)
				m_onMappingLearned(*learned);
			return true; // Consumed
		}

		// Try to find a learned mapping for this MIDI event FIRST
		// Learned mappings override default controller mappings
		const auto* mapping = preset->findMapping(_event);
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
		std::lock_guard lock(m_mutex);
		m_isLearning = true;
		m_learningParamName = _paramName;
		m_learningValues.clear();
		m_learningChannel = 0;
		m_learningController = 0;
		m_learningType = synthLib::M_CONTROLCHANGE;
	}

	void MidiLearnTranslator::cancelLearning()
	{
		std::lock_guard lock(m_mutex);
		resetLearningState();
	}

	void MidiLearnTranslator::resetLearningState()
	{
		m_isLearning = false;
		m_learningParamName.clear();
		m_learningValues.clear();
	}

	std::string MidiLearnTranslator::getLearningParamName() const
	{
		std::lock_guard lock(m_mutex);
		return m_learningParamName;
	}

	void MidiLearnTranslator::setOnMappingLearned(MappingLearnedCallback _callback)
	{
		std::lock_guard lock(m_callbackMutex);
		m_onMappingLearned = std::move(_callback);
	}

	void MidiLearnTranslator::setOnLearningProgress(LearningProgressCallback _callback)
	{
		std::lock_guard lock(m_callbackMutex);
		m_onLearningProgress = std::move(_callback);
	}

	void MidiLearnTranslator::setOnSendMidiOutput(SendMidiOutputCallback _callback)
	{
		std::lock_guard lock(m_callbackMutex);
		m_onSendMidiOutput = std::move(_callback);
	}

	void MidiLearnTranslator::setOnPresetChanged(PresetChangedCallback _callback)
	{
		std::lock_guard lock(m_callbackMutex);
		m_onPresetChanged = std::move(_callback);
	}

	bool MidiLearnTranslator::isDefaultControllerMapping(const synthLib::SMidiEvent& _event) const
	{
		// Check if this MIDI event matches any default controller mapping
		const auto& params = m_controllerMap.getParameters(_event);
		return !params.empty();
	}

	void MidiLearnTranslator::applyMapping(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event)
	{
		if (_mapping.part != MidiLearnMapping::AutoPart)
		{
			// Specific part: apply directly
			auto* param = m_controller.getParameter(_mapping.paramName, _mapping.part);
			if (param)
				applyMappingToParam(_mapping, _event, *param);
			return;
		}

		// Auto: resolve parts from incoming MIDI channel
		const auto channel = static_cast<uint8_t>(_event.a & 0x0f);
		auto parts = m_controller.getPartsForMidiChannel(channel);

		if (parts.empty())
			return;

		for (const auto part : parts)
		{
			auto* param = m_controller.getParameter(_mapping.paramName, part);
			if (!param)
				continue;

			applyMappingToParam(_mapping, _event, *param);
		}
	}

	void MidiLearnTranslator::applyMappingToParam(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event, Parameter& _param)
	{
		if (_mapping.mode == MidiLearnMapping::Mode::RelativeSigned)
		{
			// Relative Signed: 0x01 = +1 (increment), 0x7F = -1 (decrement)
			const auto value = MidiLearnMapping::getValue(_event);
			const auto currentValue = _param.getUnnormalizedValue();
			const auto& desc = _param.getDescription();

			const uint8_t incValue = _mapping.invert ? 0x7F : 0x01;
			const uint8_t decValue = _mapping.invert ? 0x01 : 0x7F;

			int newValue = currentValue;
			if (value == decValue)
				newValue = std::max(static_cast<int>(desc.range.getStart()), currentValue - 1);
			else if (value == incValue)
				newValue = std::min(static_cast<int>(desc.range.getEnd()), currentValue + 1);

			_param.setUnnormalizedValueNotifyingHost(newValue, Parameter::Origin::Midi);
		}
		else if (_mapping.mode == MidiLearnMapping::Mode::RelativeOffset)
		{
			// Relative Offset: 65 (0x41) = +1 (increment), 63 (0x3F) = -1 (decrement)
			const auto value = MidiLearnMapping::getValue(_event);
			const auto currentValue = _param.getUnnormalizedValue();
			const auto& desc = _param.getDescription();

			const uint8_t incValue = _mapping.invert ? 0x3F : 0x41;
			const uint8_t decValue = _mapping.invert ? 0x41 : 0x3F;

			int newValue = currentValue;
			if (value == decValue)
				newValue = std::max(static_cast<int>(desc.range.getStart()), currentValue - 1);
			else if (value == incValue)
				newValue = std::min(static_cast<int>(desc.range.getEnd()), currentValue + 1);

			_param.setUnnormalizedValueNotifyingHost(newValue, Parameter::Origin::Midi);
		}
		else
		{
			// Absolute mode: map MIDI value to parameter range
			const auto& desc = _param.getDescription();
			const auto rangeSize = desc.range.getEnd() - desc.range.getStart();

			float normalized;

			if (_mapping.type == MidiLearnMapping::Type::PitchBend)
			{
				// Pitch Bend: 14-bit value from LSB (b) and MSB (c)
				const uint16_t pitchValue = (static_cast<uint16_t>(_event.c) << 7) | (_event.b & 0x7f);
				normalized = pitchValue / 16383.0f;
			}
			else if (_mapping.type == MidiLearnMapping::Type::ChannelPressure)
			{
				// Channel Pressure: value in byte b
				normalized = _event.b / 127.0f;
			}
			else
			{
				// CC, PolyPressure: value in byte c
				normalized = _event.c / 127.0f;
			}

			if (_mapping.invert)
				normalized = 1.0f - normalized;

			const auto paramValue = static_cast<int>(desc.range.getStart() + normalized * rangeSize);
			_param.setUnnormalizedValueNotifyingHost(paramValue, Parameter::Origin::Midi);
		}
	}

	size_t MidiLearnTranslator::handleLearning(const synthLib::SMidiEvent& _event, std::optional<MidiLearnMapping>& _learned)
	{
		const auto statusByte = static_cast<synthLib::MidiStatusByte>(_event.a & 0xf0);

		// Only support learnable MIDI message types
		if (statusByte != synthLib::M_CONTROLCHANGE &&
		    statusByte != synthLib::M_POLYPRESSURE &&
		    statusByte != synthLib::M_AFTERTOUCH &&
		    statusByte != synthLib::M_PITCHBEND)
			return 0;

		const auto channel = MidiLearnMapping::getChannel(_event);
		const auto controller = MidiLearnMapping::getController(_event);
		const auto value = MidiLearnMapping::getValue(_event);

		// First event: establish channel, type, and controller
		if (m_learningValues.empty())
		{
			m_learningChannel = channel;
			m_learningController = controller;
			m_learningType = statusByte;
			m_learningValues.push_back(value);

			// Notify progress
			return 1;
		}

		// Subsequent events: must be same type and channel
		if (statusByte != m_learningType || channel != m_learningChannel)
			return 0;

		// For CC and PolyPressure, must also be same controller number
		if ((statusByte == synthLib::M_CONTROLCHANGE || statusByte == synthLib::M_POLYPRESSURE) &&
		    controller != m_learningController)
			return 0;

		// Only add if it's a unique value
		if (std::find(m_learningValues.begin(), m_learningValues.end(), value) == m_learningValues.end())
		{
			m_learningValues.push_back(value);
		}

		// Count unique values
		const size_t uniqueCount = m_learningValues.size();

		// Wait for enough unique values (forces user to rotate in both directions)
		if (uniqueCount < kRequiredUniqueValues)
			return uniqueCount;

		// We have enough unique values, create the mapping
		MidiLearnMapping newMapping;
		newMapping.type = MidiLearnMapping::midiStatusToType(statusByte);
		newMapping.channel = m_learningChannel;
		newMapping.controller = m_learningController;
		newMapping.paramName = m_learningParamName;

		// Only detect relative mode for CC (encoders), other types are always absolute
		if (statusByte == synthLib::M_CONTROLCHANGE)
			newMapping.mode = detectMode(m_learningValues);
		else
			newMapping.mode = MidiLearnMapping::Mode::Absolute;

		// No conflict or reassigning controller from another parameter - notify success and exit learning mode
		resetLearningState();

		_learned = std::move(newMapping);
		return uniqueCount;
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

		// If we see both very low and very high values, it's relative signed (0x01/0x7F)
		if (hasLowValues && hasHighValues)
			return MidiLearnMapping::Mode::RelativeSigned;

		// Values clustered around center (0x3F/0x41) = relative offset (63/65)
		if (hasMidValues)
			return MidiLearnMapping::Mode::RelativeOffset;

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
					const auto origin = _param->getChangeOrigin();

					// Don't send feedback for MIDI-originated changes (avoid feedback loops)
					if (origin == Parameter::Origin::Midi)
						return;

					onParameterChanged(paramName, _param->getValue(), origin);
				});

			m_paramListenerIds.emplace_back(param, listenerId);
		}
	}

	void MidiLearnTranslator::unsubscribeFromParameters()
	{
		// Remove listeners
		for (const auto& [param, listenerId] : m_paramListenerIds)
			param->onValueChanged.removeListener(listenerId);

		m_paramListenerIds.clear();
	}

	void MidiLearnTranslator::onParameterChanged(const std::string& _paramName, float _normalizedValue, Parameter::Origin _origin)
	{
		// Find all mappings for this parameter
		const auto mappings = m_preset.findMappingsByParam(_paramName);
		
		for (const auto* mapping : mappings)
		{
			if (!mapping || mapping->feedbackTargets == 0)
				continue;

			// Create the feedback MIDI event
			const auto source = Controller::parameterOriginToMidiEventSource(_origin);
			const auto feedbackEvent = createFeedbackEvent(*mapping, _normalizedValue, source);

			// Send to each enabled feedback target
			std::lock_guard lock(m_callbackMutex);
			if (!m_onSendMidiOutput)
				continue;

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Device))
				m_onSendMidiOutput(synthLib::MidiEventSource::Device, feedbackEvent);

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Editor))
				m_onSendMidiOutput(synthLib::MidiEventSource::Editor, feedbackEvent);

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Host))
				m_onSendMidiOutput(synthLib::MidiEventSource::Host, feedbackEvent);

			if (mapping->isFeedbackEnabled(synthLib::MidiEventSource::Physical))
				m_onSendMidiOutput(synthLib::MidiEventSource::Physical, feedbackEvent);
		}
	}

	synthLib::SMidiEvent MidiLearnTranslator::createFeedbackEvent(const MidiLearnMapping& _mapping, float _normalizedValue, synthLib::MidiEventSource _source) const
	{
		if (_mapping.invert)
			_normalizedValue = 1.0f - _normalizedValue;

		// Always send absolute values for feedback (even for relative-mode mappings)
		const uint8_t midiValue = static_cast<uint8_t>(std::clamp(_normalizedValue * 127.0f, 0.0f, 127.0f));

		const auto statusByte = MidiLearnMapping::typeToMidiStatus(_mapping.type);
		const uint8_t feedbackChannel = _mapping.channel == MidiLearnMapping::AllChannels ? 0 : _mapping.channel;
		const uint8_t statusWithChannel = statusByte | (feedbackChannel & 0x0f);

		// Create the event based on message type
		synthLib::SMidiEvent event(_source);
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

		// Serialize preset to JSON. The host saves on a thread of its choice, the message thread copy is not for it
		std::shared_ptr<const MidiLearnPreset> preset;
		{
			std::lock_guard lock(m_mutex);
			preset = m_activePreset;
		}
		const auto json = preset->toJson();
		const auto jsonString = juce::JSON::toString(json);
		_stream.write(jsonString.toStdString());
		
		// Save learn input sources
		_stream.write(m_learnInputSources.load());
	}

	void MidiLearnTranslator::loadChunkData(baseLib::ChunkReader& _cr)
	{
		// Version 1: Only Preset JSON
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
			
			if (_version >= 2)
			{
				// Load learn input sources
				uint8_t sources = 0;
				_stream.read(sources);
				m_learnInputSources = sources;
			}
		});
	}
}
