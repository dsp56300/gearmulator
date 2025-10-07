#pragma once

#include "jeLcd.h"
#include "jeLib/jemiditypes.h"
#include "jeLib/state.h"

#include "jeLib/sysexRemoteControl.h"

#include "jucePluginLib/controller.h"
#include "jucePluginLib/patchdb/patch.h"

namespace jeJucePlugin
{
	class AudioPluginAudioProcessor;

	class Controller : public pluginLib::Controller
	{
	public:
		Controller(AudioPluginAudioProcessor&);
		~Controller() override;

		void onStateLoaded() override;

		uint8_t getPartCount() const override
		{
			return 2;
		}

		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value, pluginLib::Parameter::Origin _origin) override;
		void sendParameterChange(jeLib::SystemParameter _parameter, int _value) const;
		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;

		bool sendSingle(const pluginLib::SysEx& _sysex, uint32_t _part) const;

		void sendTempPerformanceRequest() const;
		void sendPerformanceRequest(jeLib::AddressArea _area, jeLib::UserPerformanceArea _performance) const;
		void sendSystemRequest() const;

		jeLib::SysexRemoteControl& getSysexRemote() { return m_sysexRemote; }

		bool isUpperSelected() const;
		bool isLowerSelected() const;
		bool isBothSelected() const;

		bool requestPatchForPart(std::vector<uint8_t>& _data, uint32_t _part, uint64_t _userData) const;

	private:
		void parsePerformanceCommon(const pluginLib::SysEx& _sysex) const;
		void parsePatch(const pluginLib::SysEx& _sysex, uint8_t _part) const;
		void parsePart(const pluginLib::SysEx& _sysex, uint8_t _part) const;
		void parseSystemParameters(const pluginLib::SysEx& _sysex) const;

		jeLib::SysexRemoteControl m_sysexRemote;
		jeLib::State m_state;
	};
}
