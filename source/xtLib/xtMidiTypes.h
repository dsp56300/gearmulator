#pragma once

#include <cstdint>

#include "xtId.h"

#include "wLib/wMidiTypes.h"

namespace xt
{
	enum MidiHeaderByte : uint8_t
	{
		IdMw1 = 0x00,
		IdMw2 = 0x0e,
	};

	enum class SysexCommand : uint8_t
	{
		Invalid = 0xff,

		SingleRequest     = 0x00, SingleDump    = 0x10, SingleParameterChange    = 0x20, SingleStore    = 0x30, SingleRecall    = 0x40, SingleCompare    = 0x50,
		MultiRequest      = 0x01, MultiDump     = 0x11, MultiParameterChange     = 0x21, MultiStore     = 0x31, MultiRecall     = 0x41, MultiCompare     = 0x51,
		WaveRequest	      = 0x02, WaveDump      = 0x12, WaveParameterChange      = 0x22, WaveStore      = 0x32, WaveRecall      = 0x42, WaveCompare      = 0x52,
		WaveCtlRequest	  = 0x03, WaveCtlDump   = 0x13, WaveCtlParameterChange   = 0x23, WaveCtlStore   = 0x33, WaveCtlRecall   = 0x43, WaveCtlCompare   = 0x53,
		GlobalRequest     = 0x04, GlobalDump    = 0x14, GlobalParameterChange    = 0x24, GlobalStore    = 0x34, GlobalRecall    = 0x44, GlobalCompare    = 0x54,
		DisplayRequest    = 0x05, DisplayDump   = 0x15, DisplayParameterChange   = 0x25, DisplayStore   = 0x35, DisplayRecall   = 0x45, DisplayCompare   = 0x55,
		RemoteCtlRequest  = 0x06, RemoteCtlDump = 0x16, RemoteCtlParameterChange = 0x26, RemoteCtlStore = 0x36, RemoteCtlRecall = 0x46, RemoteCtlCompare = 0x56,
		ModeRequest       = 0x07, ModeDump      = 0x17, ModeParameterChange      = 0x27, ModeStore      = 0x37, ModeRecall      = 0x47, ModeCompare      = 0x57,
		InfoRequest       = 0x08, InfoDump      = 0x18, InfoParameterChange      = 0x28, InfoStore      = 0x38, InfoRecall      = 0x48, InfoCompare      = 0x58,

		// emu specific, these are to preview waves and wavetables, the dump format is identical to regular wave/wavetable dumps but we modify the DSP memory directly
		WaveRequestP      = 0x09, WaveDumpP     = 0x19, WaveParameterChangeP     = 0x29, WaveStoreP     = 0x39, WaveRecallP     = 0x49, WaveCompareP     = 0x59,
		WaveCtlRequestP	  = 0x0a, WaveCtlDumpP  = 0x1a, WaveCtlParameterChangeP  = 0x2a, WaveCtlStoreP  = 0x3a, WaveCtlRecallP  = 0x4a, WaveCtlCompareP  = 0x5a,

		WavePreviewMode = WaveStoreP,

		// emu remote control support
		EmuLCD = 0x60,
		EmuLEDs = 0x61,
		EmuButtons = 0x62,
		EmuRotaries = 0x63
	};

	enum class LocationH : uint8_t
	{
		// Single Dump
		SingleBankA = 0x00,
		SingleBankB = 0x01,
		AllSingles  = 0x10,
		SingleEditBufferSingleMode = 0x20,
		SingleEditBufferMultiMode = 0x30,

		// Multi Dump
		MultiBankA = 0x00,
		AllMultis = 0x10,
		MultiDumpMultiEditBuffer = 0x20,
	};

	enum SysexIndex
	{
		// first parameter of a dump
		IdxSingleParamFirst = 7,
		IdxSingleChecksumStart = IdxSingleParamFirst,
		IdxMultiParamFirst  = IdxSingleParamFirst,
		IdxMultiChecksumStart = IdxMultiParamFirst,
		IdxGlobalParamFirst = wLib::IdxBuffer,
		IdxModeParamFirst = wLib::IdxBuffer,

		IdxSingleParamIndexH = wLib::IdxBuffer + 1,
		IdxSingleParamIndexL = IdxSingleParamIndexH + 1,
		IdxSingleParamValue  = IdxSingleParamIndexL + 1,

		IdxMultiParamIndexH = wLib::IdxBuffer,
		IdxMultiParamIndexL = IdxMultiParamIndexH + 1,
		IdxMultiParamValue  = IdxMultiParamIndexL + 1,

		IdxGlobalParamIndexH = wLib::IdxBuffer,
		IdxGlobalParamIndexL = IdxGlobalParamIndexH,
		IdxGlobalParamValue  = IdxGlobalParamIndexL + 1,

		IdxModeParamIndexH = wLib::IdxBuffer,
		IdxModeParamIndexL = IdxModeParamIndexH,
		IdxModeParamValue  = wLib::IdxBuffer,

		IdxWaveIndexH = wLib::IdxBuffer,
		IdxWaveIndexL = IdxWaveIndexH + 1
	};

	enum class GlobalParameter
	{
		Reserved0,
		Version,
		StartupSoundbank,
		StartupSoundNum,
		MidiChannel,
		ProgramChangeMode,
		DeviceId,
		BendRange,
		ControllerW,
		ControllerX,
		ControllerY,
		ControllerZ,
		MainVolume,
		Reserved13,
		Reserved14,
		Transpose,
		MasterTune,
		DisplayTimeout,
		LcdContrast,
		Reserved19,
		Reserved20,
		Reserved21,
		Reserved22,
		StartupMultiNumber,
		ArpNoteOutChannel,
		MidiClockOutput,
		ParameterSend,
		ParameterReceive,
		InputGain,
		Reserved29,
		Reserved30,
		Reserved31
	};

	enum class ModeParameter
	{
		Mode = 0	// 0 = Single, 1 = Multi
	};

	enum class SingleParameter
	{
		Version = 0,
		Wavetable = 25
	};

	enum class MultiParameter
	{
		Volume,
		ControlW,
		ControlX,
		ControlY,
		ControlZ,
		ArpTempo,
		Reserved6, Reserved7, Reserved8, Reserved9, Reserved10, Reserved11, Reserved12, Reserved13, Reserved14, Reserved15,
		Name00, Name01, Name02, Name03, Name04, Name05, Name06, Name07, Name08, Name09, Name10, Name11, Name12, Name13, Name14, Name15,

		Inst0SoundBank,
		Inst0SoundNumber,
		Inst0MidiChannel,
		Inst0Volume,
		Inst0Transpose,
		Inst0Detune,
		Inst0Output,
		Inst0Status,	// off/on
		Inst0Pan,
		Inst0PanMod,	// off, on, inverse
		Inst0Reserved10,
		Inst0Reserved11,
		Inst0VeloLow,
		Inst0VeloHigh,
		Inst0KeyLow,
		Inst0KeyHigh,
		Inst0ArpActive,	// off, on, hold, Sound Arp
		Inst0ArpClock,
		Inst0ArpRange,
		Inst0ArpPattern,
		Inst0ArpDir,
		Inst0ArpNoteOrder,
		Inst0ApVelocity,
		Inst0ArpReset,
		Inst0ArpNotesOut,
		Inst0Reserved25,
		Inst0Reserved26,
		Inst0Reserved27,

		Inst1First,

		Inst0First = Inst0SoundBank,
		Inst0Last = Inst0Reserved27,
	};

	static_assert(static_cast<uint8_t>(MultiParameter::Inst1First) - static_cast<uint8_t>(MultiParameter::Inst0First) == 28);

	namespace Mw1
	{
		static constexpr uint32_t g_singleLength = 180;			// without sysex header
		static constexpr uint32_t g_singleDumpLength = 187;		// with sysex header
		static constexpr uint32_t g_singleNameLength = 16;
		static constexpr uint32_t g_singleNamePosition = 153;	// in a dump including sysex header
		static constexpr uint32_t g_sysexHeaderSize = 5;
		static constexpr uint32_t g_sysexFooterSize = 2;
		static constexpr uint32_t g_idmPresetBank = 0x50;
		static constexpr uint32_t g_idmCartridgeBank = 0x54;
		static constexpr uint32_t g_idmPreset = 0x42;
	};

	namespace wave
	{
		static constexpr uint16_t g_romWaveCount = 506;
		static constexpr uint16_t g_ramWaveCount = 250;
		static constexpr uint16_t g_firstRamWaveIndex = 1000;

		static constexpr uint16_t g_tableCount = 128;
		static constexpr uint16_t g_wavesPerTable = 64;
		static constexpr uint16_t g_firstRamTableIndex = 96;

		// these are either algorithmic or invalid, we cannot request them via MIDI
		static constexpr uint32_t g_algorithmicWavetables[] = {28, 29,
			30, 31, 32, 33, 34, 35, 36, 37, 38, 39,
			40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
			50, 51,
			64, 65, 66, 67, 68, 69,
			70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
			80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
			90, 91, 92, 93, 94, 95};

		constexpr bool isValidWaveIndex(const uint32_t _index)
		{
			if(_index >= g_firstRamWaveIndex + g_ramWaveCount)
				return false;
			if(_index >= g_romWaveCount && _index < g_firstRamWaveIndex)
				return false;
			return true;
		}

		constexpr bool isValidTableIndex(const uint32_t _index)
		{
			return _index < g_tableCount;
		}

		constexpr bool isAlgorithmicTable(const xt::TableId _index)
		{
			for (const uint32_t i : g_algorithmicWavetables)
			{
				if(_index.rawId() == i)
					return true;
			}
			return false;
		}

		constexpr bool isReadOnly(const TableId _table)
		{
			if(!_table.isValid())
				return true;
			return _table.rawId() < g_firstRamTableIndex;
		}

		constexpr bool isReadOnly(const WaveId _waveId)
		{
			if(!_waveId.isValid())
				return true;
			return _waveId.rawId() < g_firstRamWaveIndex;
		}

		constexpr bool isReadOnly(const TableIndex _index)
		{
			return _index.rawId() >= 61;	// always tri/square/saw
		}
	}
}
