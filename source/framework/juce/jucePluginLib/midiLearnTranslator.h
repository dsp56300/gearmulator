#pragma once

#include "midiLearnPreset.h"
#include "parameter.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
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
	//
	// processMidiInput() runs on whatever thread the MIDI arrives on: the audio thread, a MIDI port's thread or the MCP
	// server. It reads a snapshot of the preset, so a preset change never pulls the mappings out from under it. The
	// parameter subscriptions for feedback and getPreset() belong to the message thread.
	class MidiLearnTranslator
	{
	public:
		using MappingLearnedCallback = std::function<void(const MidiLearnMapping&)>;
		using LearningProgressCallback = std::function<void(size_t, size_t)>;
		using SendMidiOutputCallback = std::function<void(synthLib::MidiEventSource, const synthLib::SMidiEvent&)>;
		using PresetChangedCallback = std::function<void()>;

		MidiLearnTranslator(Controller& _controller, const ControllerMap& _controllerMap);
		~MidiLearnTranslator();

		// Preset management. Any thread may set it, MIDI input uses it right away, the subscriptions follow on the
		// message thread
		void setPreset(const MidiLearnPreset& _preset);
		// message thread only
		const MidiLearnPreset& getPreset() const { return m_preset; }

		// MIDI input processing
		bool processMidiInput(const synthLib::SMidiEvent& _event);

		// Learning mode
		void startLearning(const std::string& _paramName);
		void cancelLearning();
		bool isLearning() const { return m_isLearning; }
		std::string getLearningParamName() const;

		// Learn input source filtering (bitmask of MidiEventSource)
		uint8_t getLearnInputSources() const { return m_learnInputSources; }
		void setLearnInputSources(uint8_t _sources) { m_learnInputSources = _sources; }

		// Mapping learned and learning progress are called on the thread the MIDI arrives on, the others on the
		// message thread. Setting one waits for a call of it that is in progress.
		void setOnMappingLearned(MappingLearnedCallback _callback);
		void setOnLearningProgress(LearningProgressCallback _callback);
		void setOnSendMidiOutput(SendMidiOutputCallback _callback);
		void setOnPresetChanged(PresetChangedCallback _callback);

		// State persistence
		void saveChunkData(baseLib::BinaryStream& _stream) const;
		void loadChunkData(baseLib::ChunkReader& _cr);

	private:
		bool isDefaultControllerMapping(const synthLib::SMidiEvent& _event) const;
		void applyMapping(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event);
		void applyMappingToParam(const MidiLearnMapping& _mapping, const synthLib::SMidiEvent& _event, Parameter& _param);
		// with m_mutex held. Returns the progress to report, 0 for nothing
		size_t handleLearning(const synthLib::SMidiEvent& _event, std::optional<MidiLearnMapping>& _learned);
		void resetLearningState();
		void applyPreset(const MidiLearnPreset& _preset);
		
		MidiLearnMapping::Mode detectMode(const std::vector<uint8_t>& _values) const;

		// Parameter feedback
		void subscribeToParameters();
		void unsubscribeFromParameters();
		void onParameterChanged(const std::string& _paramName, float _normalizedValue, Parameter::Origin _origin);
		synthLib::SMidiEvent createFeedbackEvent(const MidiLearnMapping& _mapping, float _normalizedValue, synthLib::MidiEventSource _source) const;

		Controller& m_controller;
		const ControllerMap& m_controllerMap;

		// message thread copy
		MidiLearnPreset m_preset;

		// guards m_activePreset and the learning state
		mutable std::mutex m_mutex;
		std::shared_ptr<const MidiLearnPreset> m_activePreset;

		// held while a callback is set or called
		std::recursive_mutex m_callbackMutex;
		MappingLearnedCallback m_onMappingLearned;
		LearningProgressCallback m_onLearningProgress;
		SendMidiOutputCallback m_onSendMidiOutput;
		PresetChangedCallback m_onPresetChanged;

		// a preset set off the message thread reaches the subscriptions later, but not after this is gone
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

		// Learning state
		std::atomic<bool> m_isLearning = false;
		std::string m_learningParamName;
		std::vector<uint8_t> m_learningValues; // Collect values to detect mode
		uint8_t m_learningChannel = 0;
		uint8_t m_learningController = 0;
		synthLib::MidiStatusByte m_learningType = synthLib::M_CONTROLCHANGE;
		std::atomic<uint8_t> m_learnInputSources = (1<<static_cast<uint8_t>(synthLib::MidiEventSource::Host)) | (1<<static_cast<uint8_t>(synthLib::MidiEventSource::Physical)); // Host | Physical - both enabled by default
		static constexpr size_t kRequiredUniqueValues = 2; // Need 2 unique values (both directions)

		// Parameter subscriptions for feedback. The parameter is kept with its listener: the settings page edits the
		// mappings in place before it calls setPreset(), so they no longer tell which parameters were subscribed
		std::vector<std::pair<Parameter*, baseLib::Event<Parameter*>::ListenerId>> m_paramListenerIds;

		// Cache for quick lookup: key = (channel << 8) | controller
		std::unordered_map<uint32_t, size_t> m_midiToMappingIndex;
	};
}
