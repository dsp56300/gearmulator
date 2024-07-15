#pragma once

#include "parameterdescriptions.h"
#include "parameter.h"
#include "parameterlocking.h"
#include "softknob.h"

#include "synthLib/midiTypes.h"

#include <string>

#include "parameterlinks.h"

namespace juce
{
	class AudioProcessor;
	class Value;
}

namespace pluginLib
{
	class Processor;
	using SysEx = std::vector<uint8_t>;

	class Controller : juce::Timer
	{
	public:
		static constexpr uint32_t InvalidParameterIndex = 0xffffffff;

		explicit Controller(Processor& _processor, const std::string& _parameterDescJson);
		~Controller() override;

		virtual void sendParameterChange(const Parameter& _parameter, uint8_t _value) = 0;
		void sendLockedParameters(uint8_t _part);

        juce::Value* getParamValueObject(uint32_t _index, uint8_t _part) const;
        Parameter* getParameter(uint32_t _index) const;
        Parameter* getParameter(uint32_t _index, uint8_t _part) const;
        Parameter* getParameter(const std::string& _name, uint8_t _part) const;
		
        uint32_t getParameterIndexByName(const std::string& _name) const;

		bool setParameters(const std::map<std::string, uint8_t>& _values, uint8_t _part, Parameter::Origin _changedBy) const;

		const MidiPacket* getMidiPacket(const std::string& _name) const;

		bool createNamedParamValues(MidiPacket::NamedParamValues& _params, const std::string& _packetName, uint8_t _part) const;
		bool createNamedParamValues(MidiPacket::NamedParamValues& _dest, const MidiPacket::AnyPartParamValues& _source) const;
		bool createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _data, uint8_t _part) const;
		bool createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _data, const MidiPacket::NamedParamValues& _values) const;
		bool createMidiDataFromPacket(std::vector<uint8_t>& _sysex, const std::string& _packetName, const std::map<MidiDataType, uint8_t>& _data, const MidiPacket::AnyPartParamValues& _values) const;

		bool parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;
		bool parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, MidiPacket::AnyPartParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;
		bool parseMidiPacket(const MidiPacket& _packet, MidiPacket::Data& _data, const std::function<void(MidiPacket::ParamIndex, uint8_t)>& _parameterValues, const std::vector<uint8_t>& _src) const;
		bool parseMidiPacket(const std::string& _name, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;
		bool parseMidiPacket(std::string& _name, MidiPacket::Data& _data, MidiPacket::ParamValues& _parameterValues, const std::vector<uint8_t>& _src) const;

		const auto& getExposedParameters() const { return m_synthParams; }

		uint8_t getCurrentPart() const { return m_currentPart; }
		void setCurrentPart(const uint8_t _part) { m_currentPart = _part; }
		virtual uint8_t getPartCount() { return 16; }

		virtual bool parseSysexMessage(const SysEx&, synthLib::MidiEventSource) = 0;
		virtual bool parseControllerMessage(const synthLib::SMidiEvent&) = 0;

		virtual bool parseMidiMessage(const synthLib::SMidiEvent& _e);

		virtual void onStateLoaded() = 0;

        // this is called by the plug-in on audio thread!
        void enqueueMidiMessages(const std::vector<synthLib::SMidiEvent>&);

		void loadChunkData(synthLib::ChunkReader& _cr);
		void saveChunkData(synthLib::BinaryStream& s);

	private:
		void getMidiMessages(std::vector<synthLib::SMidiEvent>&);
		void processMidiMessages();

	public:
		ParameterLocking& getParameterLocking() { return m_locking; }
		ParameterLinks& getParameterLinks() { return m_parameterLinks; }

		std::set<std::string> getRegionIdsForParameter(const Parameter* _param) const;
		std::set<std::string> getRegionIdsForParameter(const std::string& _name) const;

		const ParameterDescriptions& getParameterDescriptions() const { return m_descriptions; }

		const SoftKnob* getSoftknob(const Parameter* _parameter) const
		{
			const auto it = m_softKnobs.find(_parameter);
			return it->second.get();
		}

		Processor& getProcessor() const { return m_processor; }

	protected:
		virtual Parameter* createParameter(Controller& _controller, const Description& _desc, uint8_t _part, int _uid);
		void registerParams(juce::AudioProcessor& _processor);

		void sendSysEx(const pluginLib::SysEx &) const;
		bool sendSysEx(const std::string& _packetName) const;
		bool sendSysEx(const std::string& _packetName, const std::map<pluginLib::MidiDataType, uint8_t>& _params) const;
		void sendMidiEvent(const synthLib::SMidiEvent& _ev) const;
		void sendMidiEvent(uint8_t _a, uint8_t _b, uint8_t _c, uint32_t _offset = 0, synthLib::MidiEventSource _source = synthLib::MidiEventSource::Editor) const;

		bool combineParameterChange(uint8_t& _result, const std::string& _midiPacket, const Parameter& _parameter, uint8_t _value) const;

		virtual bool isDerivedParameter(Parameter& _derived, Parameter& _base) const { return true; }

        struct ParamIndex
        {
            uint8_t page;
            uint8_t partNum;
            uint8_t paramNum;

        	bool operator<(const ParamIndex& _p) const
            {
				if (page < _p.page)         return true;
				if (page > _p.page)         return false;
				if (partNum < _p.partNum)   return true;
				if (partNum > _p.partNum)   return false;
				if (paramNum < _p.paramNum) return true;
				if (paramNum > _p.paramNum) return false;
				return false;
			}

			bool operator==(const ParamIndex& _p) const
            {
	            return page == _p.page && partNum == _p.partNum && paramNum && _p.paramNum;
            }
        };

		using ParameterList = std::vector<Parameter*>;

		void timerCallback() override;

	private:
		Processor& m_processor;
        ParameterDescriptions m_descriptions;

		uint8_t m_currentPart = 0;

		std::mutex m_midiMessagesLock;
        std::vector<synthLib::SMidiEvent> m_midiMessages;

		std::map<const Parameter*, std::unique_ptr<SoftKnob>> m_softKnobs;

	protected:
		// tries to find synth param in both internal and host
		const ParameterList& findSynthParam(uint8_t _part, uint8_t _page, uint8_t _paramIndex) const;
		const ParameterList& findSynthParam(const ParamIndex& _paramIndex) const;

    	std::map<ParamIndex, ParameterList> m_synthInternalParams;
		std::map<ParamIndex, ParameterList> m_synthParams; // exposed and managed by audio processor
		std::array<ParameterList, 16> m_paramsByParamType;
		std::vector<std::unique_ptr<Parameter>> m_synthInternalParamList;
		ParameterLocking m_locking;
		ParameterLinks m_parameterLinks;
	};
}
