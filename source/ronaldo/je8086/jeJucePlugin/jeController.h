#pragma once

#include "jeLcd.h"

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
		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;

		bool sendSingle(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) const;

		jeLib::SysexRemoteControl& getSysexRemote() { return m_sysexRemote; }

	private:
		jeLib::SysexRemoteControl m_sysexRemote;
	};
}
