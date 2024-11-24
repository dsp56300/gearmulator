#pragma once

#include <array>
#include <cstdint>
#include <set>
#include <vector>

namespace synthLib
{
	struct SMidiEvent;

	// This class receives midi events and can transform the incoming midi channels to one or more target channels.
	// Furthermore, it can be remote-controlled via sysex messages
	class MidiTranslator
	{
	public:
		// https://midi.org/sysexidtable we use the first "Reserved for Other Uses" ID but as these
		// messages never leave the plugin but are completely internal only it doesn't really matter anyway
		static constexpr uint8_t ManufacturerId = 0x60;

		enum Command : uint8_t
		{
			CmdSkipTranslation  = 0x01,	// f0, ManufacturerId, CmdSkipTranslation, statusbyte, byte2, byte3, f7
			CmdAddTargetChannel,		// f0, ManufacturerId, CmdAddTargetChannel, sourceChannel, targetChannel, f7
			CmdClearTargetChannels,		// f0, ManufacturerId, CmdClearTargetChannels, f7
			CmdResetTargetChannels,		// f0, ManufacturerId, CmdResetTargetChannels, f7
		};

		MidiTranslator();
		MidiTranslator(const MidiTranslator&) = default;
		MidiTranslator(MidiTranslator&&) = default;

		virtual ~MidiTranslator() = default;

		MidiTranslator& operator = (const MidiTranslator&) = default;
		MidiTranslator& operator = (MidiTranslator&&) = default;

		virtual void process(std::vector<SMidiEvent>& _results, const SMidiEvent& _source);

		bool addTargetChannel(uint8_t _sourceChannel, uint8_t _targetChannel);

		void reset();
		void clear();

		static SMidiEvent& createPacketSkipTranslation(SMidiEvent& _ev);
		static SMidiEvent createPacketSetTargetChannel(uint8_t _sourceChannel, uint8_t _targetChannel);

	private:
		std::array<std::set<uint8_t>, 16> m_targetChannels;
	};
}
