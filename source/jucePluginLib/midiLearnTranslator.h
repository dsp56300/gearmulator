#pragma once

#include "midiLearnPreset.h"
#include "parameter.h"

#include <functional>
#include <unordered_map>

#include "synthLib/midiTypes.h"

namespace baseLib
{
	class BinaryStream;
	class ChunkReader;
}

namespace pluginLib
{
	class Controller;
	class ControllerMap;

	// Translates MIDI input to parameter changes and handles learning mode
	// Phase 1: CC support only, future phases will add other MIDI message types
	class MidiLearnTranslator
	{
	public:
		MidiLearnTranslator(Controller& _controller, const ControllerMap& _controllerMap);
		~MidiLearnTranslator();

		// Preset management
		void setPreset(const MidiLearnPreset& _preset);
		const MidiLearnPreset& getPreset() const { return m_preset; }

		// MIDI input processing
		bool processMidiInput(const synthLib::SMidiEvent& _event);

		// Learning mode
		void startLearning(const std::string& _paramName);
		void cancelLearning();
		bool isLearning() const { return m_isLearning; }
		const std::string& getLearningParamName() const { return m_learningParamName; }

		// Learn input source filtering (bitmask of MidiEventSource)
		uint8_t getLearnInputSources() const { return m_learnInputSources; }
		void setLearnInputSources(uint8_t _sources) { m_learnInputSources = _sources; }

		// Events/Callbacks
		std::function<void(const MidiLearnMapping&)> onMappingLearned;
		std::function<void(const MidiLearnMapping&)> onMappingConflict;
		std::function<void(size_t, size_t)> onLearningProgress;
		std::function<void(synthLib::MidiEventSource, const synthLib::SMidiEvent&)> onSendMidiOutput;

		// State persistence
		void saveChunkData(baseLib::BinaryStream& _stream) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

	private:
		bool isDefaultControllerMapping(const synthLib::SMidiEvent& _event) const;
		void applyMapping(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event);
		void handleLearning(const synthLib::SMidiEvent& _event);
		
		MidiLearnMapping::Mode detectMode(const std::vector<uint8_t>& _values) const;

		// Parameter feedback
		void subscribeToParameters();
		void unsubscribeFromParameters();
		void onParameterChanged(const std::string& _paramName, float _normalizedValue);
		synthLib::SMidiEvent createFeedbackEvent(const MidiLearnMapping& _mapping, float _normalizedValue) const;

		Controller& m_controller;
		const ControllerMap& m_controllerMap;
		MidiLearnPreset m_preset;

		// Learning state
		bool m_isLearning = false;
		std::string m_learningParamName;
		std::vector<uint8_t> m_learningValues; // Collect values to detect mode
		uint8_t m_learningChannel = 0;
		uint8_t m_learningController = 0;
		synthLib::MidiStatusByte m_learningType = synthLib::M_CONTROLCHANGE;
		uint8_t m_learnInputSources = (1<<static_cast<uint8_t>(synthLib::MidiEventSource::Host)) | (1<<static_cast<uint8_t>(synthLib::MidiEventSource::Physical)); // Host | Physical - both enabled by default
		static constexpr size_t kRequiredUniqueValues = 2; // Need 2 unique values (both directions)

		// Parameter subscriptions for feedback
		std::vector<baseLib::Event<Parameter*>::ListenerId> m_paramListenerIds;

		// Cache for quick lookup: key = (channel << 8) | controller
		std::unordered_map<uint32_t, size_t> m_midiToMappingIndex;
	};
}
