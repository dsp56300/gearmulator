#pragma once

#include "jeLcd.h"
#include "jeLib/jemiditypes.h"

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

		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value) override;
		void sendParameterChange(jeLib::SystemParameter _parameter, int _value) const;
		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;

		bool sendSingle(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) const;

		void sendTempPerformanceRequest() const;
		void sendPerformanceRequest(jeLib::AddressArea _area, jeLib::UserPerformanceArea _performance) const;
		void sendSystemRequest() const;

		jeLib::SysexRemoteControl& getSysexRemote() { return m_sysexRemote; }

	private:
		void parsePerformanceCommon(const pluginLib::SysEx& _sysex) const;
		void parsePatch(const pluginLib::SysEx& _sysex, uint8_t _part) const;
		void parsePart(const pluginLib::SysEx& _sysex, uint8_t _part) const;
		void parseSystemParameters(const pluginLib::SysEx& _sysex);

		jeLib::SysexRemoteControl m_sysexRemote;
	};
}
