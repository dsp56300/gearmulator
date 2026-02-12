#pragma once

#include "midiLearnPreset.h"
#include "parameter.h"

#include <functional>
#include <unordered_map>

namespace synthLib { struct SMidiEvent; }

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

		// Events/Callbacks
		std::function<void(const MidiLearnMapping&)> onMappingLearned;
		std::function<void(const MidiLearnMapping&)> onMappingConflict;
		std::function<void(synthLib::MidiEventSource, const synthLib::SMidiEvent&)> onSendMidiOutput;

	private:
		bool isDefaultControllerMapping(const synthLib::SMidiEvent& _event) const;
		void applyMapping(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event);
		void handleLearning(const synthLib::SMidiEvent& _event);
		
		MidiLearnMapping::Mode detectMode(uint8_t _value) const;

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

		// Parameter subscriptions for feedback
		std::vector<baseLib::Event<Parameter*>::ListenerId> m_paramListenerIds;

		// Cache for quick lookup: key = (channel << 8) | controller
		std::unordered_map<uint32_t, size_t> m_midiToMappingIndex;
	};
}
