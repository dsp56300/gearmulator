#pragma once

#include "jucePluginLib/controller.h"

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
	};
}
