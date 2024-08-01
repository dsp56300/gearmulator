#pragma once

#include "jucePluginLib/controller.h"
#include "n2xLib/n2xstate.h"

class AudioPluginAudioProcessor;

namespace n2xJucePlugin
{
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

		Controller(AudioPluginAudioProcessor&);
		~Controller() override;

		static std::string loadParameterDescriptions();

		void onStateLoaded() override
		{
		}

		uint8_t getPartCount() override
		{
			return 4;
		}

		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;
		bool parseSingleDump(const pluginLib::SysEx& _msg);
		bool parseMultiDump(const pluginLib::SysEx& _msg);
		bool parseControllerMessage(const synthLib::SMidiEvent&) override;

		void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
		void setMultiParameter(n2x::MultiParam _mp, uint8_t _value);
		void sendMultiParameter(const pluginLib::Parameter& _parameter, uint8_t _value);
		bool sendSysEx(MidiPacketType _packet, const std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

		void requestDump(uint8_t _bank, uint8_t _patch) const;

		std::vector<uint8_t> createSingleDump(uint8_t _bank, uint8_t _program, uint8_t _part) const;
		bool activatePatch(const std::vector<uint8_t>& _sysex, uint32_t _part);

		bool isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const override;

	private:
		n2x::State m_state;
		pluginLib::EventListener<uint8_t> m_currentPartChanged;
	};
}
