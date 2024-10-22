#pragma once

#include "jucePluginLib/controller.h"

#include "jucePluginLib/event.h"

namespace xt
{
	enum class GlobalParameter;
	enum class LocationH : uint8_t;
}

namespace xtJucePlugin
{
	class WaveEditor;
	class FrontPanel;

	class AudioPluginAudioProcessor;

	class Controller : public pluginLib::Controller
	{
	public:
		enum MidiPacketType
		{
			RequestSingle,
			RequestMulti,
			RequestSingleBank,
			RequestMultiBank,
			RequestGlobal,
			RequestMode,
			RequestAllSingles,
			SingleParameterChange,
			MultiParameterChange,
			GlobalParameterChange,
			SingleDump,
			MultiDump,
			GlobalDump,
			ModeDump,
	        EmuRequestLcd,
	        EmuRequestLeds,
	        EmuSendButton,
	        EmuSendRotary,
			RequestWave,
			WaveDump,
			RequestTable,
			TableDump,

			Count
		};

		struct Patch
		{
			std::string name;
			std::vector<uint8_t> data;
		};

		pluginLib::Event<bool> onPlayModeChanged;
		pluginLib::Event<uint8_t> onProgramChanged;

		Controller(AudioPluginAudioProcessor &, unsigned char _deviceId = 0);
		~Controller() override;

		bool sendSingle(const std::vector<uint8_t>& _sysex);
		bool sendSingle(const std::vector<uint8_t>& _sysex, uint8_t _part);

		bool sendSysEx(MidiPacketType _type) const;
		bool sendSysEx(MidiPacketType _type, std::map<pluginLib::MidiDataType, uint8_t>& _params) const;
		using pluginLib::Controller::sendSysEx;

		bool isMultiMode() const;
		void setPlayMode(bool _multiMode);

		void selectNextPreset();
		void selectPrevPreset();

		std::vector<uint8_t> createSingleDump(xt::LocationH _buffer, uint8_t _location, uint8_t _part) const;
		std::vector<uint8_t> createSingleDump(xt::LocationH _buffer, uint8_t _location, const pluginLib::MidiPacket::AnyPartParamValues& _values) const;
		bool parseSingle(pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _paramValues, const std::vector<uint8_t>& _sysex) const;

		std::string getSingleName(uint8_t _part) const;
		std::string getSingleName(const pluginLib::MidiPacket::ParamValues& _values) const;
		std::string getSingleName(const pluginLib::MidiPacket::AnyPartParamValues& _values) const;
		std::string getString(const pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix, size_t _len) const;

		bool setSingleName(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _value) const;
		bool setString(pluginLib::MidiPacket::AnyPartParamValues& _values, const std::string& _prefix, size_t _len, const std::string& _value) const;

		void setFrontPanel(xtJucePlugin::FrontPanel* _frontPanel);
		void setWaveEditor(xtJucePlugin::WaveEditor* _waveEditor);

		bool requestWave(uint32_t _number) const;
		bool requestTable(uint32_t _number) const;

		uint8_t getPartCount() const override;

	private:
		void selectPreset(int _offset);

		void onStateLoaded() override;

		void parseSingle(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params);
		void parseMulti(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params) const;
		void parseGlobal(const pluginLib::SysEx& _msg, const pluginLib::MidiPacket::Data& _data, const pluginLib::MidiPacket::ParamValues& _params);

		bool parseMidiPacket(MidiPacketType _type, pluginLib::MidiPacket::Data& _data, pluginLib::MidiPacket::AnyPartParamValues& _params, const pluginLib::SysEx& _sysex) const;

		bool parseSysexMessage(const pluginLib::SysEx&, synthLib::MidiEventSource) override;
		bool parseControllerMessage(const synthLib::SMidiEvent&) override;

		void sendParameterChange(const pluginLib::Parameter& _parameter, pluginLib::ParamValue _value) override;
		bool sendGlobalParameterChange(xt::GlobalParameter _param, uint8_t _value);
		bool sendModeDump() const;
		void requestSingle(xt::LocationH _buf, uint8_t _location) const;
		void requestMulti(xt::LocationH _buf, uint8_t _location) const;

		uint8_t getGlobalParam(xt::GlobalParameter _type) const;

		bool isDerivedParameter(pluginLib::Parameter& _derived, pluginLib::Parameter& _base) const override;

		void requestAllPatches() const;

		const uint8_t m_deviceId;

		Patch m_singleEditBuffer;
		std::array<Patch,8> m_singleEditBuffers;
		std::array<uint8_t, 39> m_globalData{};
		std::array<uint8_t, 1> m_modeData{};
		std::array<uint32_t, 8> m_currentSingles{0};
		uint32_t m_currentSingle = 0;
		xtJucePlugin::FrontPanel* m_frontPanel = nullptr;
		xtJucePlugin::WaveEditor* m_waveEditor = nullptr;
	};
}