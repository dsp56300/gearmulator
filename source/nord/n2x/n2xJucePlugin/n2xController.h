#pragma once

#include "jucePluginLib/controller.h"
#include "n2xLib/n2xstate.h"

namespace n2xJucePlugin
{
	class AudioPluginAudioProcessor;

	class Controller : public pluginLib::Controller
	{
	public:
		enum class MidiPacketType
		{
			RequestDump,
			SingleDump,
			MultiDump,

			Count
		};

		pluginLib::Event<> onProgramChanged;
		pluginLib::Event<n2x::KnobType, uint8_t> onKnobChanged;

		Controller(AudioPluginAudioProcessor&);
		~Controller() override;

		void onStateLoaded() override;

		uint8_t getPartCount() const override
		{
			return 4;
		}

		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;
		bool parseSingleDump(const pluginLib::SysEx& _msg);
		bool parseMultiDump(const pluginLib::SysEx& _msg);
		bool parseControllerMessage(const synthLib::SMidiEvent&) override;

		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value) override;

		void setSingleParameter(uint8_t _part, n2x::SingleParam _sp, uint8_t _value);
		void setMultiParameter(n2x::MultiParam _mp, uint8_t _value);
		uint8_t getMultiParameter(n2x::MultiParam _param) const;

		void sendMultiParameter(const pluginLib::Parameter& _parameter, uint8_t _value);
		bool sendSysEx(MidiPacketType _packet, const std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

		void requestDump(uint8_t _bank, uint8_t _patch) const;

		std::vector<uint8_t> createSingleDump(uint8_t _bank, uint8_t _program, uint8_t _part) const;
		std::vector<uint8_t> createMultiDump(n2x::SysexByte _bank, uint8_t _program);

		bool activatePatch(const std::vector<uint8_t>& _sysex, uint32_t _part);

		bool isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const override;

		std::string getSingleName(uint8_t _part) const;
		std::string getPatchName(uint8_t _part) const;

		using pluginLib::Controller::sendSysEx;

		bool getKnobState(uint8_t& _result, n2x::KnobType _type) const;

	private:
		uint8_t combineSyncRingModDistortion(uint8_t _part, uint8_t _currentCombinedValue, bool _lockedOnly);

		n2x::State m_state;
		pluginLib::EventListener<uint8_t> m_currentPartChanged;
	};
}
