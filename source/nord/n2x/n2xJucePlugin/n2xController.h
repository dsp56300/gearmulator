#pragma once

#include "jucePluginLib/controller.h"

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

	Controller(AudioPluginAudioProcessor&);
	~Controller() override;

private:
	static std::string loadParameterDescriptions();

	void onStateLoaded() override
	{
	}

	uint8_t getPartCount() override
	{
		return 4;
	}

	bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;
	bool parseControllerMessage(const synthLib::SMidiEvent&) override;

	void sendParameterChange(const pluginLib::Parameter& _parameter, uint8_t _value) override;
	bool sendSysEx(MidiPacketType _packet, const std::map<pluginLib::MidiDataType, uint8_t>& _params) const;

	void requestDump(uint8_t _bank, uint8_t _patch) const;
};
