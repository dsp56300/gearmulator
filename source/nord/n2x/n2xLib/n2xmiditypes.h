#pragma once

#include <cstdint>

namespace n2x
{
	enum SysexByte : uint8_t
	{
		IdClavia = 0x33, IdN2X = 0x04, DefaultDeviceId = 0xf,

		SingleDumpBankEditBuffer    = 0x00, SingleDumpBankA    = 0x01, SingleDumpBankB    = 0x02, SingleDumpBankC    = 0x03, SingleDumpBankD    = 0x04,
		SingleRequestBankEditBuffer = 0x0e, SingleRequestBankA = 0x0f, SingleRequestBankB = 0x10, SingleRequestBankC = 0x11, SingleRequestBankD = 0x12,

		MultiDumpBankEditBuffer    = 30, MultiDumpBankA,
		MultiRequestBankEditBuffer = 40,

		EmuSetPotPosition = 90,		// total dump is: f0, IdClavia, IdDevice, IdN2x, EmuSetPotPosition, KnobType / nibble high, nibble low / f7
		EmuGetPotsPosition = 91,
		EmuSetPartCC = 92,
	};

	enum SysexIndex
	{
		IdxClavia = 1,
		IdxDevice,
		IdxN2x,
		IdxMsgType,
		IdxMsgSpec,
		IdxKnobPosH,
		IdxKnobPosL
	};

	enum SingleParam
	{
		Gain			= 17,
		OctaveShift		= 64,
		ModWheelDest	= 59,
		Unison			= 60,
		VoiceMode		= 58,
		Auto			= 62,
		Portamento		= 16,
		Lfo1Rate		= 21,
		Lfo1Waveform	= 56,
		Lfo1Dest		= 57,
		Lfo1Level		= 22,
		Lfo2Rate		= 23,
		Lfo2Dest		= 65,
		ArpRange		= 24,
		ModEnvA			= 18,
		ModEnvD			= 19,
		ModEnvDest		= 61,
		ModEnvLevel		= 20,
		O1Waveform		= 50,
		O2Waveform		= 51,
		O2Pitch			=  0,
		O2PitchFine		=  1,
		FmDepth			=  7,
		O2Keytrack		= 54,
		PW				=  6,
		Sync			= 52,
		Mix				=  2,
		AmpEnvA			= 12,
		AmpEnvD			= 13,
		AmpEnvS			= 14,
		AmpEnvR			= 15,
		FilterEnvA		=  8,
		FilterEnvD		=  9,
		FilterEnvS		= 10,
		FilterEnvR		= 11,
		FilterType		= 53,
		Cutoff			=  3,
		Resonance		=  4,
		FilterEnvAmount	=  5,
		FilterVelocity	= 63,
		FilterKeytrack	= 55,
		Distortion		= 52
	};

	enum ControlChange
	{
		CCGain					=  7, 
		CCOctaveShift			= 17, 
		CCModWheelDest			= 18, 
		CCUnison				= 16, 
		CCVoiceMode				= 15, 
		CCAuto					= 65, 
		CCPortamento			=  5, 
		CCLfo1Rate				= 19, 
		CCLfo1Waveform			= 20, 
		CCLfo1Dest				= 21, 
		CCLfo1Level				= 22, 
		CCLfo2Rate				= 23, 
		CCLfo2Dest				= 24, 
		CCArpRange				= 25, 
		CCModEnvA				= 26, 
		CCModEnvD				= 27, 
		CCModEnvDest			= 28, 
		CCModEnvLevel			= 29, 
		CCO1Waveform			= 30, 
		CCO2Waveform			= 31, 
		CCO2Pitch				= 78, 
		CCO2PitchFine			= 33, 
		CCFmDepth				= 70, 
		CCO2Keytrack			= 34, 
		CCPW					= 79, 
		CCSync					= 35, 
		CCMix					=  8, 
		CCAmpEnvA				= 73, 
		CCAmpEnvD				= 36, 
		CCAmpEnvS				= 37, 
		CCAmpEnvR				= 72, 
		CCFilterEnvA			= 38, 
		CCFilterEnvD			= 39, 
		CCFilterEnvS			= 40, 
		CCFilterEnvR			= 41, 
		CCFilterType			= 44, 
		CCCutoff				= 74, 
		CCResonance				= 42, 
		CCFilterEnvAmount		= 43, 
		CCFilterVelocity		= 45, 
		CCFilterKeytrack		= 46, 
		CCDistortion			= 80, 
	};

	enum MultiParam
	{
		SlotAMidiChannel					= 264,
		SlotALfo1Sync						= 268,
		SlotALfo2Sync						= 272,
		SlotAFilterEnvTrigger				= 276,
		SlotAFilterEnvTrigMidiChannel		= 280,
		SlotAFilterEnvTrigMidiNoteNumber	= 284,
		SlotAAmpEnvTrigger					= 288,
		SlotAAmpEnvTrigMidiChannel			= 292,
		SlotAAmpEnvTrigMidiNoteNumber		= 296,
		SlotAMorphTrigger					= 300,
		SlotAMorphTrigMidiChannel			= 304,
		SlotAMorphTrigMidiNoteNumber		= 308,
		BendRange							= 312,
		UnisonDetune,
		OutModeABCD,
		GlobalMidiChannel,
		MidiProgramChange,
		MidiControl,
		MasterTune,
		PedalType,
		LocalControl,
		KeyboardOctaveShift,
		SelectedChannel,
		ArpMidiOut,
		SlotAChannelActive,
		SlotAProgramSelect					= 328,
		SlotABankSelect						= 332,
		SlotAChannelPressureAmount			= 336,
		SlotAChannelPressureDest			= 340,
		SlotAExpressionPedalAmount			= 344,
		SlotAExpressionPedalDest			= 348,
		KeyboardSplitActive					= 352,
		KeyboardSplitPointPoint
	};

	static constexpr uint32_t g_sysexHeaderSize = 6;												// F0, IdClavia, IdDevice, IdN2x, MsgType, MsgSpec
	static constexpr uint32_t g_sysexFooterSize = 1;												// F7
	static constexpr uint32_t g_sysexContainerSize = g_sysexHeaderSize + g_sysexFooterSize;

	static constexpr uint32_t g_singleDataSize = 66 * 2;											// 66 parameters in two nibbles
	static constexpr uint32_t g_multiDataSize = 4 * g_singleDataSize + 90 * 2;						// 4 singles and 90 params in two nibbles

	static constexpr uint32_t g_nameLength = 10;	// Aura editor custom format adds 10 bytes for the name
	static constexpr uint32_t g_singleDumpSize = g_singleDataSize + g_sysexContainerSize;
	static constexpr uint32_t g_singleDumpWithNameSize = g_singleDumpSize + g_nameLength;
	static constexpr uint32_t g_multiDumpSize  = g_multiDataSize  + g_sysexContainerSize;
	static constexpr uint32_t g_multiDumpWithNameSize = g_multiDumpSize + g_nameLength;
	static constexpr uint32_t g_patchRequestSize = g_sysexContainerSize;

	static constexpr uint32_t g_singleBankCount = 10;
	static constexpr uint32_t g_multiBankCount = 4;
	static constexpr uint32_t g_programsPerBank = 99;
}
