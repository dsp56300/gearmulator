#pragma once

namespace mqLib
{
	enum class MidiDataType : uint8_t
	{
		SingleRequest = 0x00, SingleDump = 0x10, SingleParameterChange = 0x20, SingleParameterRequest = 0x30,
		MultiRequest  = 0x01, MultiDump  = 0x11, MultiParameterChange  = 0x21, MultiParameterRequest  = 0x31,
		DrumRequest	  = 0x02, DrumDump   = 0x12, DrumParameterChange   = 0x22, DrumParameterRequest   = 0x32,
		GlobalRequest = 0x04, GlobalDump = 0x14, GlobalParameterChange = 0x24, GlobalParameterRequest = 0x34,
	};

	enum class MidiBufferNum : uint8_t
	{
		DeprecatedSingleBankA = 0x00,
		DeprecatedSingleBankB,
		DeprecatedSingleBankC,
		DeprecatedSingleBankX,
		AllSounds = 0x10,
		EditBufferSingle = 0x20,
		EditBufferMulti = 0x30,
		EditBufferSingleLayer = 0x30,
		EditBufferDrumMap = 0x30,
		SingleBankA = 0x40,
		SingleBankB,
		SingleBankC,
		SingleBankX = 0x48
	};

	enum class MidiSoundLocation : uint8_t
	{
		AllSinglesBankA = 0x40,
		AllSinglesBankB,
		AllSinglesBankC,
		AllSinglesBankX = 0x48,
		EditBufferCurrentSingle = 0x00,
		EditBufferFirstMulti = 0x00,
		EditBufferFirstSingleLayer = 0x00,
		EditBufferFirstDrumMapInstrument = 0x10,
	};
	
	enum class GlobalParameter : uint8_t
	{
		// Global data
		Version,

		// Initial instrument settings
		InstrumentSelection = 20,
		SingleMultiMode = 21,
		MultiNumber = 22,

		// Instr. 1-4
		InstrumentASingleNumber = 1,	InstrumentBSingleNumber,	InstrumentCSingleNumber,	InstrumentDSingleNumber,
		InstrumentABankNumber = 9,		InstrumentBBankNumber,		InstrumentCBankNumber,		InstrumentDBankNumber,

		// Pedal/CV
		PedalOffset = 70,
		PedalGain,
		PedalCurve,
		PedalControl,

		// MIDI Setup
		Tuning = 5,
		Transpose,
		ControllerSend,
		ControllerReceive,
		ControllerW = 53,
		ControllerX,
		ControllerY,
		ControllerZ,
		ArpSend = 15,
		Clock = 19,
		MidiChannel = 24,
		SysExDeviceId,
		LocalControl,

		// Program change
		ProgramChangeRx = 57,
		ProgramChangeTx = 74,

		// Display Setup
		PopupTime = 27,
		LabelTime,
		DisplayContrast,

		// Keyboard setp
		OnVelocityCurve = 30,
		ReleaseVelocityCurve,
		PressureCurve,

		// External input
		InputGain = 33,

		// FX setup
		GlobalLinkFX2 = 35,

		// Mix in
		Send = 58,
		Level
	};
}
