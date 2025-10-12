#pragma once

#include <cstdint>

namespace jeLib
{
	enum class AddressArea : uint32_t
	{
		System             = 0x00000000,
		PerformanceTemp    = 0x01000000,
		UserPatch          = 0x02000000, // User Patch (Patch U:A11 - U:B88)
		UserPerformance    = 0x03000000, // User Performance (Performance U:11 - U:88)
		PatternData        = 0x08000000, // Pattern Data (Pattern 1 - 48) - only keyboard?
		MotionControlDataA = 0x09000000, // Motion Control Data (Motion Set A)
		MotionControlDataB = 0x0A000000, // Motion Control Data (Motion Set B) - only rack?
		Invalid			   = 0xFF000000,
		Mask               = 0xFF000000,
	};

	enum class SystemArea : uint16_t
	{
		SystemParameter = 0x00000000,
		PatternSetup    = 0x00001000,
		MotionSetup     = 0x00002000,
		TxRxSetting     = 0x00003000,
	};

	enum class UserPatchArea : uint32_t
	{
		UserPatch001 = 0x00000000, // A11
		UserPatch002 = 0x00000200, // A12
		UserPatch064 = 0x00007E00, // A88
		UserPatch065 = 0x00010000, // B11
		UserPatch066 = 0x00010200, // B12
		UserPatch128 = 0x00017E00, // B88

		BlockSize = 0x00000200,    // 512 bytes
		BlockMask = BlockSize - 1, // 511
	};

	enum class UserPerformanceArea : uint32_t
	{
		UserPerformance01 = 0x00000000, // 11
		UserPerformance02 = 0x00010000, // 12
		UserPerformance64 = 0x003F0000, // 88

		BlockSize = 0x00010000,         // 65536 bytes
		BlockMask = BlockSize - 1,      // 65535
	};

	enum class MotionControlData : uint32_t
	{
		MotionControlData1 = 0x00000000,
		MotionControlData2 = 0x00400000,
	};

	enum SystemParameter : uint8_t
	{
		PerformanceBank,				// User, Preset
		PerformanceNumber,				// 11 - 88
		PerformanceControlChannel,		// 1 - 16, Off
		PowerUpMode,					// Perform P:11, Last-Set
		MidiSync,						// Off, On
		LocalSwitch,					// Off, On
		TxRxEditMode,					// Mode 1, Mode 2
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

	enum PatternSetup : uint8_t // total size 48 bytes
	{
		Pattern01LoopLength = 0x00, // 1- 4 (measure)
		Pattern02LoopLength,        // 1- 4 (measure)
		// ... up to 48
	};
	
	enum MotionSetup : uint8_t // total size 4 bytes
	{
		MotionControllerA1LoopLength = 0x0000, // 1 - 8 (measure)
		MotionControllerA2LoopLength,          // 1 - 8 (measure)
		MotionControllerB1LoopLength,          // 1 - 8 (measure)
		MotionControllerB2LoopLength,          // 1 - 8 (measure)
	};

	enum class TxRxSetting : uint8_t
	{
	    Lfo1Rate = 0x00000000,              // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Lfo1Fade = 0x00000001,              // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Lfo2Rate = 0x00000002,              // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    CrossModulationDepth = 0x00000003,  // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    OscillatorBalance = 0x00000004,     // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    OscLfo1Depth = 0x00000005,          // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    PitchLfo2Depth = 0x00000006,        // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    PitchEnvDepth = 0x00000007,         // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    PitchEnvAttackTime = 0x00000008,    // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    PitchEnvDecayTime = 0x00000009,     // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Osc1Control1 = 0x0000000A,          // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Osc1Control2 = 0x0000000B,          // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Osc2Range = 0x0000000C,             // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Osc2FineTune = 0x0000000D,          // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Osc2Control1 = 0x0000000E,          // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Osc2Control2 = 0x0000000F,          // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    CutoffFrequency = 0x00000010,       // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    Resonance = 0x00000011,             // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    CutoffKeyFollow = 0x00000012,       // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterLfo1Depth = 0x00000013,       // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterLfo2Depth = 0x00000014,       // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterEnvDepth = 0x00000015,        // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterEnvAttackTime = 0x00000016,   // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterEnvDecayTime = 0x00000017,    // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterEnvSusLevel = 0x00000018,     // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    FilterEnvRelTime = 0x00000019,      // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmplifierLevel = 0x0000001A,        // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmplifierLfo1Depth = 0x0000001B,    // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmplifierLfo2Depth = 0x0000001C,    // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmpEnvAttackTime = 0x0000001D,      // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmpEnvDecayTime = 0x0000001E,       // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmpEnvSusLevel = 0x0000001F,        // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    AmpEnvReleaseTime = 0x00000020,     // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    ToneControlBass = 0x00000021,       // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    ToneControlTreble = 0x00000022,     // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    MultiEffectsLevel = 0x00000023,     // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    DelayTime = 0x00000024,             // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    DelayFeedback = 0x00000025,         // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    DelayLevel = 0x00000026,            // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    PortamentoTime = 0x00000027,        // 00h - 60h | OFF, CC#1-31, AFTER, CC#33-95, SYSEX
	    MorphControlUp = 0x00000028,        // 00h - 5Fh | OFF, CC#1-31, AFTER, CC#33-95
	    MorphControlDown = 0x00000029       // 00h - 5Fh | OFF, CC#1-31, AFTER, CC#33-95
	};

	enum class PerformanceData : uint16_t
	{
		PerformanceCommon = 0x0000,
		VoiceModulator    = 0x0800,
		PartUpper         = 0x1000,
		PartLower         = 0x1100,
		PatchUpper        = 0x4000,
		PatchLower        = 0x4200,

		BlockMask         = 0xff00,
	};

	enum class PerformanceCommon : uint8_t
	{
	    PerformanceName1 = 0x00000000,                      // 20h - 7Dh | ASCII Code
	    PerformanceName2 = 0x00000001,                      // 20h - 7Dh | ASCII Code
	    PerformanceName3 = 0x00000002,                      // 20h - 7Dh | ASCII Code
	    PerformanceName4 = 0x00000003,                      // 20h - 7Dh | ASCII Code
	    PerformanceName5 = 0x00000004,                      // 20h - 7Dh | ASCII Code
	    PerformanceName6 = 0x00000005,                      // 20h - 7Dh | ASCII Code
	    PerformanceName7 = 0x00000006,                      // 20h - 7Dh | ASCII Code
	    PerformanceName8 = 0x00000007,                      // 20h - 7Dh | ASCII Code
	    PerformanceName9 = 0x00000008,                      // 20h - 7Dh | ASCII Code
	    PerformanceName10 = 0x00000009,                     // 20h - 7Dh | ASCII Code
	    PerformanceName11 = 0x0000000A,                     // 20h - 7Dh | ASCII Code
	    PerformanceName12 = 0x0000000B,                     // 20h - 7Dh | ASCII Code
	    PerformanceName13 = 0x0000000C,                     // 20h - 7Dh | ASCII Code
	    PerformanceName14 = 0x0000000D,                     // 20h - 7Dh | ASCII Code
	    PerformanceName15 = 0x0000000E,                     // 20h - 7Dh | ASCII Code
	    PerformanceName16 = 0x0000000F,                     // 20h - 7Dh | ASCII Code
	    KeyMode = 0x00000010,                               // 00h - 02h | SINGLE, DUAL, SPLIT
	    SplitPoint = 0x00000011,                            // 00h - 7Fh | C-1 - G9 (only in SPLIT mode)
	    PanelSelect = 0x00000012,                           // 00h - 02h | UPPER, LOWER, UPPER&LOWER
	    PartDetune = 0x00000013,                            // 00h - 64h | -50 - +50
	    OutputAssign = 0x00000014,                          // 00h - 01h | MIX OUT, PARALLEL OUT
	    ArpeggioDestination = 0x00000015,                   // 00h - 02h | LOWER&UPPER, LOWER, UPPER (*)
	    VoiceAssign = 0x00000016,                           // 00h - 06h | 8-2, 7-3, 5-5, 3-7, 2-8, 6-4, 4-6 (**)
	    ArpeggioSwitch = 0x00000017,                        // 00h - 01h | OFF, ON
	    ArpeggioMode = 0x00000018,                          // 00h - 04h | UP, DOWN, UP&DOWN, RANDOM, RPS
	    ArpeggioBeatPattern = 0x00000019,                   // 00h - 59h | 1/4, 1/6, ... SEQUENCE-A1, ..., RANDOM
	    ArpeggioOctaveRange = 0x0000001A,                   // 00h - 03h | 1 - 4 [octave]
	    ArpeggioHold = 0x0000001B,                          // 00h - 01h | OFF, ON
	    // 00 00 00 1C | --- | --- | ---
	    IndividualTriggerSwitch = 0x0000001D,               // 00h - 01h | OFF, ON
	    IndividualTriggerDestination = 0x0000001E,          // 00h - 02h | FILTER ENV, AMPLITUDE ENV, FILTER&AMP
	    IndividualTriggerSourceChannel = 0x0000001F,        // 00h - 0Fh | 1 - 16
	    IndividualTriggerSourceNote = 0x00000020,           // 00h - 80h | 0 - 127 as C-1 - G9, and 128 as ALL
	    Tempo = 0x00000022,                                 // 14h - FAh | 20 - 250 [beat per minute]
	    VocalUpperInputJackFrontRear = 0x00000024,          // 00h - 01h | REAR, FRONT

		DataLengthKeyboard = 0x00000024,
		DataLengthRack = 0x00000025,
	};

	enum class VoiceModulator : uint8_t
	{
	    VoiceModulatorSwitch = 0x00000000,                  // 00h - 01h | OFF, ON
	    VoiceModulatorPanelMode = 0x00000001,               // 00h - 01h | OFF, ON
	    Algorithm = 0x00000002,                             // 00h - 04h | SOLID, SMOOTH, ..., FILTER BANK NARROW
	    VoiceModulatorDelayType = 0x00000003,               // 00h - 04h | PANNING L->R - MONO LONG
	    EnsembleType = 0x00000004,                          // 00h - 0Eh | ENSEMBLE MILD, ..., FREEZE PHASE 2
	    ExternalToInstSendSwitch = 0x00000005,              // 00h - 01h | OFF, ON
	    ExternalToVocalSendSwitch = 0x00000006,             // 00h - 01h | OFF, ON
	    VocalMorphControlSwitch = 0x00000007,               // 00h - 01h | OFF, ON
	    VocalMorphThreshold = 0x00000008,                   // 00h - 7Fh | 0 - 127
	    VocalMorphSensitivity = 0x00000009,                 // 00h - 7Fh | -64 - +63
	    Control1Assign = 0x0000000A,                        // 00h - 1Ah | ENSEMBLE LEVEL - CHARACTER 12
	    Control2Assign = 0x0000000B,                        // 00h - 1Ah | ENSEMBLE LEVEL - CHARACTER 12
	    Character1 = 0x0000000C,                            // 00h - 7Fh | 0 - 127
	    Character2 = 0x0000000D,                            // 00h - 7Fh | 0 - 127
	    Character3 = 0x0000000E,                            // 00h - 7Fh | 0 - 127
	    Character4 = 0x0000000F,                            // 00h - 7Fh | 0 - 127
	    Character5 = 0x00000010,                            // 00h - 7Fh | 0 - 127
	    Character6 = 0x00000011,                            // 00h - 7Fh | 0 - 127
	    Character7 = 0x00000012,                            // 00h - 7Fh | 0 - 127
	    Character8 = 0x00000013,                            // 00h - 7Fh | 0 - 127
	    Character9 = 0x00000014,                            // 00h - 7Fh | 0 - 127
	    Character10 = 0x00000015,                           // 00h - 7Fh | 0 - 127
	    Character11 = 0x00000016,                           // 00h - 7Fh | 0 - 127
	    Character12 = 0x00000017,                           // 00h - 7Fh | 0 - 127
	    VocalMix = 0x00000018,                              // 00h - 7Fh | 0 - 127
	    VoiceModulatorRelease = 0x00000019,                 // 00h - 7Fh | 0 - 127
	    VoiceModulatorResonance = 0x0000001A,               // 00h - 7Fh | 0 - 127
	    VoiceModulatorPan = 0x0000001B,                     // 00h - 7Fh | L64 - R63
	    VoiceModulatorLevel = 0x0000001C,                   // 00h - 7Fh | 0 - 127
	    VoiceModulatorNoiseCutoff = 0x0000001D,             // 00h - 7Fh | 0 - 127
	    VoiceModulatorNoiseLevel = 0x0000001E,              // 00h - 7Fh | 0 - 127
	    GateThreshold = 0x0000001F,                         // 00h - 7Fh | 0 - 127
	    RobotPitch = 0x00000020,                            // 00h - 7Fh | 0 - 127
	    RobotControl = 0x00000021,                          // 00h - 7Fh | 0 - 127
	    RobotLevel = 0x00000022,                            // 00h - 7Fh | 0 - 127
	    EnsembleLevel = 0x00000023,                         // 00h - 7Fh | 0 - 127
	    VoiceModulatorDelayTime = 0x00000024,               // 00h - 7Fh | 0 - 127
	    VoiceModulatorDelayFeedback = 0x00000025,           // 00h - 7Fh | 0 - 127
	    VoiceModulatorDelayLevel = 0x00000026,              // 00h - 7Fh | 0 - 127
	    EnsembleSync = 0x00000027,                          // 00h - 16h | OFF, 1/16, 1/8(3), ..., 8MEASURES
	    VoiceModulatorDelaySync = 0x00000028,               // 00h - 0Ah | OFF, 1/16, 1/8(3), ..., 1/2

		DataLengthKeyboard = 0,
		DataLengthRack = 0x00000029,
	};

	enum class Part : uint8_t
	{
	    PatchBank = 0x00000000,                             // 00h - 03h | IN PERFORMANCE, USER, PRESET, CARD (*)
	    PatchNo = 0x00000001,                               // 00h - 7Fh | A11 - B88 (*)
	    MidiChannel = 0x00000002,                           // 00h - 10h | 1 - 16, OFF
	    PartTranspose = 0x00000003,                         // 00h - 30h | -24 - + 24 [semitone]
	    DelaySync = 0x00000004,                             // 00h - 0Ah | OFF, 1/16, 1/8(3), ..., 1/2
	    LfoSync = 0x00000005,                               // 00h - 16h | OFF, 1/16, 1/8(3), ..., 8 MEAS
	    ChorusSync = 0x00000006,                            // 00h - 17h | OFF, 1/16, 1/8(3), ..., 8 MEAS, LFO1
	    PatchGroupNo = 0x00000007,                          // 00h - 3Fh | Group 1 - Group 64 (**)

		DataLengthKeyboard = 0x00000007,
		DataLengthRack = 0x00000008,
	};

	enum class Patch : uint16_t
	{
	    PatchName1 = 0x00000000,                           // 20h - 7Dh | ASCII Code
	    PatchName2 = 0x00000001,                           // 20h - 7Dh | ASCII Code
	    PatchName3 = 0x00000002,                           // 20h - 7Dh | ASCII Code
	    PatchName4 = 0x00000003,                           // 20h - 7Dh | ASCII Code
	    PatchName5 = 0x00000004,                           // 20h - 7Dh | ASCII Code
	    PatchName6 = 0x00000005,                           // 20h - 7Dh | ASCII Code
	    PatchName7 = 0x00000006,                           // 20h - 7Dh | ASCII Code
	    PatchName8 = 0x00000007,                           // 20h - 7Dh | ASCII Code
	    PatchName9 = 0x00000008,                           // 20h - 7Dh | ASCII Code
	    PatchName10 = 0x00000009,                          // 20h - 7Dh | ASCII Code
	    PatchName11 = 0x0000000A,                          // 20h - 7Dh | ASCII Code
	    PatchName12 = 0x0000000B,                          // 20h - 7Dh | ASCII Code
	    PatchName13 = 0x0000000C,                          // 20h - 7Dh | ASCII Code
	    PatchName14 = 0x0000000D,                          // 20h - 7Dh | ASCII Code
	    PatchName15 = 0x0000000E,                          // 20h - 7Dh | ASCII Code
	    PatchName16 = 0x0000000F,                          // 20h - 7Dh | ASCII Code
	    Lfo1Waveform = 0x00000010,                         // 00h - 03h | TRI, SAW, SQR, S/H
	    Lfo1Rate = 0x00000011,                             // 00h - 7Fh | 0 - 127
	    Lfo1Fade = 0x00000012,                             // 00h - 7Fh | 0 - 127
	    Lfo2Rate = 0x00000013,                             // 00h - 7Fh | 0 - 127
	    Lfo2DepthSelect = 0x00000014,                      // 00h - 02h | PITCH, FILTER, AMPLIFIER
	    RingModulatorSwitch = 0x00000015,                  // 00h - 01h | OFF, ON
	    CrossModulationDepth = 0x00000016,                 // 00h - 7Fh | 0 - 127
	    OscillatorBalance = 0x00000017,                    // 00h - 7Fh | -64(OSC1) - +63(OSC2)
	    Lfo1AndEnvelopeDestination = 0x00000018,           // 00h - 02h | OSC1+2, OSC2, X-MOD DEPTH
	    OscLfo1Depth = 0x00000019,                         // 00h - 7Fh | -64 - +63
	    PitchLfo2Depth = 0x0000001A,                       // 00h - 7Fh | -64 - +63
	    PitchEnvelopeDepth = 0x0000001B,                   // 00h - 7Fh | -64 - +63
	    PitchEnvelopeAttackTime = 0x0000001C,              // 00h - 7Fh | 0 - 127
	    PitchEnvelopeDecayTime = 0x0000001D,               // 00h - 7Fh | 0 - 127
	    Osc1Waveform = 0x0000001E,                         // 00h - 06h | SUPER SAW, TWM, ..., PULSE, SAW, TRI
	    Osc1Control1 = 0x0000001F,                         // 00h - 7Fh | 0 - 127
	    Osc1Control2 = 0x00000020,                         // 00h - 7Fh | 0 - 127
	    Osc2Waveform = 0x00000021,                         // 00h - 03h | PULSE, TRI, SAW, NOISE (*)
	    Osc2SyncSwitch = 0x00000022,                       // 00h - 01h | OFF, ON
	    Osc2Range = 0x00000023,                            // 00h - 32h | -WIDE, -24 - +24, +WIDE
	    Osc2FineWide = 0x00000024,                         // 00h - 64h | -50 - +50 [cent]
	    Osc2Control1 = 0x00000025,                         // 00h - 7Fh | 0 - 127
	    Osc2Control2 = 0x00000026,                         // 00h - 7Fh | 0 - 127
	    FilterType = 0x00000027,                           // 00h - 02h | HPF, BPF, LPF
	    CutoffSlope = 0x00000028,                          // 00h - 01h | -12, -24 [dB/oct]
	    CutoffFrequency = 0x00000029,                      // 00h - 7Fh | 0 - 127
	    Resonance = 0x0000002A,                            // 00h - 7Fh | 0 - 127
	    CutoffFrequencyKeyFollow = 0x0000002B,             // 00h - 7Fh | -64 - +63
	    FilterLfo1Depth = 0x0000002C,                      // 00h - 7Fh | -64 - +63
	    FilterLfo2Depth = 0x0000002D,                      // 00h - 7Fh | -64 - +63
	    FilterEnvelopeDepth = 0x0000002E,                  // 00h - 7Fh | -64 - +63
	    FilterEnvelopeAttackTime = 0x0000002F,             // 00h - 7Fh | 0 - 127
	    FilterEnvelopeDecayTime = 0x00000030,              // 00h - 7Fh | 0 - 127
	    FilterEnvelopeSustainLevel = 0x00000031,           // 00h - 7Fh | 0 - 127
	    FilterEnvelopeReleaseTime = 0x00000032,            // 00h - 7Fh | 0 - 127
	    AmpLevel = 0x00000033,                             // 00h - 7Fh | 0 - 127
	    AmpLfo1Depth = 0x00000034,                         // 00h - 7Fh | -64 - +63 (**)
	    AmpLfo2Depth = 0x00000035,                         // 00h - 7Fh | -64 - +63
	    AmpEnvelopeAttackTime = 0x00000036,                // 00h - 7Fh | 0 - 127
	    AmpEnvelopeDecayTime = 0x00000037,                 // 00h - 7Fh | 0 - 127
	    AmpEnvelopeSustainLevel = 0x00000038,              // 00h - 7Fh | 0 - 127
	    AmpEnvelopeReleaseTime = 0x00000039,               // 00h - 7Fh | 0 - 127
	    AutoPanManualPanSwitch = 0x0000003A,               // 00h - 02h | OFF, AUTO PAN, MANUAL PAN (**)
	    ToneControlBass = 0x0000003B,                      // 00h - 7Fh | -64 - +63
	    ToneControlTreble = 0x0000003C,                    // 00h - 7Fh | -64 - +63
	    MultiEffectsType = 0x0000003D,                     // 00h - 0Ch | SUPER CHORUS SLW, ..., DISTORTION
	    MultiEffectsLevel = 0x0000003E,                    // 00h - 7Fh | 0 - 127
	    DelayType = 0x0000003F,                            // 00h - 04h | PANNING L->R, ..., MONO LONG
	    DelayTime = 0x00000040,                            // 00h - 7Fh | 0 - 127
	    DelayFeedback = 0x00000041,                        // 00h - 7Fh | 0 - 127
	    DelayLevel = 0x00000042,                           // 00h - 7Fh | 0 - 127
	    BendRangeUp = 0x00000043,                          // 00h - 18h | 0 - 24 [semitone]
	    BendRangeDown = 0x00000044,                        // 00h - 18h | 0 - 24 [semitone]
	    PortamentoSwitch = 0x00000045,                     // 00h - 01h | OFF, ON
	    PortamentoTime = 0x00000046,                       // 00h - 7Fh | 0 - 127
	    MonoSwitch = 0x00000047,                           // 00h - 01h | OFF, ON
	    LegatoSwitch = 0x00000048,                         // 00h - 01h | OFF, ON
	    OscillatorShift = 0x00000049,                      // 00h - 04h | -2 - +2 [octave]
	    ControlLfo1Rate = 0x0000004A,                      // 00h - FEh | -127 - +127
	    ControlLfo1Fade = 0x0000004C,                      // 00h - FEh | -127 - +127
	    ControlLfo2Rate = 0x0000004E,                      // 00h - FEh | -127 - +127
	    ControlCrossModulationDepth = 0x00000050,          // 00h - FEh | -127 - +127
	    ControlOscillatorBalance = 0x00000052,             // 00h - FEh | -127 - +127
	    ControlPitchLfo1Depth = 0x00000054,                // 00h - FEh | -127 - +127
	    ControlPitchLfo2Depth = 0x00000056,                // 00h - FEh | -127 - +127
	    ControlPitchEnvelopeDepth = 0x00000058,            // 00h - FEh | -127 - +127
	    ControlPitchEnvelopeAttackTime = 0x0000005A,       // 00h - FEh | -127 - +127
	    ControlPitchEnvelopeDecayTime = 0x0000005C,        // 00h - FEh | -127 - +127
	    ControlOsc1Control1 = 0x0000005E,                  // 00h - FEh | -127 - +127
	    ControlOsc1Control2 = 0x00000060,                  // 00h - FEh | -127 - +127
	    ControlOsc2Range = 0x00000062,                     // 4Dh - B1h | - 50 - + 50
	    ControlOsc2FineWide = 0x00000064,                  // 1Bh - E3h | -100 - +100
	    ControlOsc2Control1 = 0x00000066,                  // 00h - FEh | -127 - +127
	    ControlOsc2Control2 = 0x00000068,                  // 00h - FEh | -127 - +127
	    ControlCutoffFrequency = 0x0000006A,               // 00h - FEh | -127 - +127
	    ControlResonance = 0x0000006C,                     // 00h - FEh | -127 - +127
	    ControlCutoffFreqKeyFollow = 0x0000006E,           // 00h - FEh | -127 - +127
	    ControlFilterLfo1Depth = 0x00000070,               // 00h - FEh | -127 - +127
	    ControlFilterLfo2Depth = 0x00000072,               // 00h - FEh | -127 - +127
	    ControlFilterEnvDepth = 0x00000074,                // 00h - FEh | -127 - +127
	    ControlFilterEnvAttackTime = 0x00000076,           // 00h - FEh | -127 - +127
	    ControlFilterEnvDecayTime = 0x00000078,            // 00h - FEh | -127 - +127
	    ControlFilterEnvSustainLevel = 0x0000007A,         // 00h - FEh | -127 - +127
	    ControlFilterEnvReleaseTime = 0x0000007C,          // 00h - FEh | -127 - +127
	    ControlAmpLevel = 0x0000007E,                      // 00h - FEh | -127 - +127

		ControlAmpLfo1Depth = 0x00000100,                  // 00h - FEh | -127 - +127
	    ControlAmpLfo2Depth = 0x00000102,                  // 00h - FEh | -127 - +127
	    ControlAmpEnvAttackTime = 0x00000104,              // 00h - FEh | -127 - +127
	    ControlAmpEnvDecayTime = 0x00000106,               // 00h - FEh | -127 - +127
	    ControlAmpEnvSustainLevel = 0x00000108,            // 00h - FEh | -127 - +127
	    ControlAmpEnvReleaseTime = 0x0000010A,             // 00h - FEh | -127 - +127
	    ControlToneControlBass = 0x0000010C,               // 00h - FEh | -127 - +127
	    ControlToneControlTreble = 0x0000010E,             // 00h - FEh | -127 - +127
	    ControlMultiEffectsLevel = 0x00000110,             // 00h - FEh | -127 - +127
	    ControlDelayTime = 0x00000112,                     // 00h - FEh | -127 - +127
	    ControlDelayFeedback = 0x00000114,                 // 00h - FEh | -127 - +127
	    ControlDelayLevel = 0x00000116,                    // 00h - FEh | -127 - +127
	    MorphBendAssign = 0x00000118,                      // 00h - 01h | OFF, ON
	    ControlPortamentoTime = 0x00000119,                // 00h - FEh | -127 - +127
	    VelocitySwitch = 0x0000011B,                       // 00h - 01h | OFF, ON
	    VelocityLfo1Rate = 0x0000011C,                     // 00h - FEh | -127 - +127
	    VelocityLfo1Fade = 0x0000011E,                     // 00h - FEh | -127 - +127
	    VelocityLfo2Rate = 0x00000120,                     // 00h - FEh | -127 - +127
	    VelocityCrossModulationDepth = 0x00000122,         // 00h - FEh | -127 - +127
	    VelocityOscillatorBalance = 0x00000124,            // 00h - FEh | -127 - +127
	    VelocityPitchLfo1Depth = 0x00000126,               // 00h - FEh | -127 - +127
	    VelocityPitchLfo2Depth = 0x00000128,               // 00h - FEh | -127 - +127
	    VelocityPitchEnvelopeDepth = 0x0000012A,           // 00h - FEh | -127 - +127
	    VelocityPitchEnvelopeAttackTime = 0x0000012C,      // 00h - FEh | -127 - +127
	    VelocityPitchEnvelopeDecayTime = 0x0000012E,       // 00h - FEh | -127 - +127
	    VelocityOsc1Control1 = 0x00000130,                 // 00h - FEh | -127 - +127
	    VelocityOsc1Control2 = 0x00000132,                 // 00h - FEh | -127 - +127
	    VelocityOsc2Range = 0x00000134,                    // 4Dh - B1h | -50 - +50
	    VelocityOsc2FineWide = 0x00000136,                 // 1Bh - E3h | -100 - +100
	    VelocityOsc2Control1 = 0x00000138,                 // 00h - FEh | -127 - +127
	    VelocityOsc2Control2 = 0x0000013A,                 // 00h - FEh | -127 - +127
	    VelocityCutoffFrequency = 0x0000013C,              // 00h - FEh | -127 - +127
	    VelocityResonance = 0x0000013E,                    // 00h - FEh | -127 - +127
	    VelocityCutoffFreqKeyFollow = 0x00000140,          // 00h - FEh | -127 - +127
	    VelocityFilterLfo1Depth = 0x00000142,              // 00h - FEh | -127 - +127
	    VelocityFilterLfo2Depth = 0x00000144,              // 00h - FEh | -127 - +127
	    VelocityFilterEnvDepth = 0x00000146,               // 00h - FEh | -127 - +127
	    VelocityFilterEnvAttackTime = 0x00000148,          // 00h - FEh | -127 - +127
	    VelocityFilterEnvDecayTime = 0x0000014A,           // 00h - FEh | -127 - +127
	    VelocityFilterEnvSusLevel = 0x0000014C,            // 00h - FEh | -127 - +127
	    VelocityFilterEnvReleaseTime = 0x0000014E,         // 00h - FEh | -127 - +127
	    VelocityAmpLevel = 0x00000150,                     // 00h - FEh | -127 - +127
	    VelocityAmpLfo1Depth = 0x00000152,                 // 00h - FEh | -127 - +127
	    VelocityAmpLfo2Depth = 0x00000154,                 // 00h - FEh | -127 - +127
	    VelocityAmpEnvAttackTime = 0x00000156,             // 00h - FEh | -127 - +127
	    VelocityAmpEnvDecayTime = 0x00000158,              // 00h - FEh | -127 - +127
	    VelocityAmpEnvSustainLevel = 0x0000015A,           // 00h - FEh | -127 - +127
	    VelocityAmpEnvReleaseTime = 0x0000015C,            // 00h - FEh | -127 - +127
	    VelocityToneControlBass = 0x0000015E,              // 00h - FEh | -127 - +127
	    VelocityToneControlTreble = 0x00000160,            // 00h - FEh | -127 - +127
	    VelocityMultiEffectsLevel = 0x00000162,            // 00h - FEh | -127 - +127
	    VelocityDelayTime = 0x00000164,                    // 00h - FEh | -127 - +127
	    VelocityDelayFeedback = 0x00000166,                // 00h - FEh | -127 - +127
	    VelocityDelayLevel = 0x00000168,                   // 00h - FEh | -127 - +127
	    VelocityPortamentoTime = 0x0000016A,               // 00h - FEh | -127 - +127
	    ActiveIndicatorOfBender = 0x0000016C,              // 00h - 01h | NOT ACTIVE, ACTIVE (***)
	    ActiveIndicatorOfVelocityAssign = 0x0000016D,      // 00h - 01h | NOT ACTIVE, ACTIVE (***)
	    ActiveIndicatorOfControlAssign = 0x0000016E,       // 00h - 01h | NOT ACTIVE, ACTIVE (***)
	    EnvelopeTypeInSolo = 0x0000016F,                   // 00h - 01h | STANDARD, ANALOG
	    Reserved170 = 0x00000170,
	    Osc2ExternalInputSwitch = 0x00000171,              // 00h - 01h | OFF, ON (*)
	    VoiceModulatorSendSwitch = 0x00000172,             // 00h - 01h | OFF, ON
	    UnisonSwitch = 0x00000173,                         // 00h - 01h | OFF, ON
	    UnisonDetune = 0x00000174,                         // 00h - 32h | 0 - 50 [cent]
	    PatchGain = 0x00000175,                            // 00h - 02h | 0dB, +6dB, +12dB
	    ExternalTriggerSwitch = 0x00000176,                // 00h - 01h | OFF, ON
	    ExternalTriggerDestination = 0x00000177,           // 00h - 02h | FILTER, AMP, FILTER&AMP

		DataLengthKeyboard = 239,
		DataLengthRack = 248,
		DataLengthLimitPerDump = 242						// aka 0x172 in 7 bit bytes
	};

	enum class SysexByte : uint8_t
	{
		SOX = 0xf0,

		ManufacturerID = 0x41,

		DeviceIdDefault = 0x10,
		DeviceIdMin = 0x10,
		DeviceIdMax = 0x1F,
		DeviceIdBroadcast = 0x7F,

		ModelIdMSB = 0x00,
		ModelIdLSB = 0x06,

		CommandIdDataRequest1 = 0x11,
		CommandIdDataSet1 = 0x12,

		Checksum = 0x00, // Placeholder for checksum byte

		EOX = 0xf7
	};

	static constexpr SysexByte g_sysexHeader[] = {SysexByte::SOX, SysexByte::ManufacturerID, SysexByte::DeviceIdDefault, SysexByte::ModelIdMSB, SysexByte::ModelIdLSB};
	static constexpr SysexByte g_sysexFooter[] = {SysexByte::Checksum, SysexByte::EOX};
}
