#pragma once

#include <cstdint>

namespace jeLib
{
	enum AddressArea : uint32_t
	{
		System             = 0x00000000,
		PerformanceTemp    = 0x01000000,
		UserPatch          = 0x02000000, // User Patch (Patch U:A11 - U:B88)
		UserPerformance    = 0x03000000, // User Performance (Performance U:11 - U:88)
		PatternData        = 0x08000000, // Pattern Data (Pattern 1 - 48) - only keyboard?
		MotionControlDataA = 0x09000000, // Motion Control Data (Motion Set A)
		MotionControlDataB = 0x0A000000, // Motion Control Data (Motion Set B) - only rack?
	};

	enum SystemArea : uint16_t
	{
		SystemParameter = 0x00000000,
		PatternSetup    = 0x00001000,
		MotionSetup     = 0x00002000,
		TxRxSetting     = 0x00003000,
	};

	enum SystemParameter : uint8_t
	{
		PerformanceBank,				// User, Preset
		PerformanceNumber,				// 11 - 88
		PerformanceControlChannel,		// 1 - 16, Off
		PowerUpMode,					// Perform P:11, Last-Set
		MidiSync,						// Off, On
		LocalSwitch,					// Off, On
		TxRxEditMode,					// Mode 1, Model 2
		TxRxEditSwitch,					// Off, On 
		TxRxProgramChangeSwitch,		// Off, PC, Bank Sel + PC
		RemoteControlChannel,			// 1 - 16, All, Off
		MasterTune,						// 427.5 - 452.9 (Hz)
		PatternTriggerQuantize,			// Off, Beat, Measure
		MotionRestart,					// Off, On
		MotionSet,						// Set A, Set B
		GateTimeRatio,					// Real, Staccato, 33%, 50%, 66%, 100%
		InputQuantize,					// Off, 1/16(3), 1/16, 1/8(3), ..., 1/4
		PatternMetronome,				// Type1 Vol4 - 1, Off, Type2 Vol1 - 4
		MotionMetronome,				// Type1 Vol4 - 1, Off, Type2 Vol1 - 4
		FactoryPresetMenu,		        // (keyboard only) Patch:Temp, ..., Motion:Set B-2, F.Preset
		BulkDumpMenu,			        // (keyboard only) All, Patch:User All, ..., Motion:Set B-2
		KeyboardShift,			        // (keyboard only) -2 - +2 (octave)
		RibbonRelative,			        // (keyboard only) Off, On
		RibbonHold,				        // (keyboard only) Off, On
		PerformanceGroupNumber,	        // (rack only) Group 1..64
		RemoteKeyboardChannel,	        // (rack only) 1-16, All
	};
}
