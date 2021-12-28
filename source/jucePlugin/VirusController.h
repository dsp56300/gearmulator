#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../synthLib/plugin.h"
#include "VirusParameter.h"

class AudioPluginAudioProcessor;

namespace Virus
{
	enum ParameterType
	{
		Param_BankSelect,
		Param_ModWheel,
		Param_BreathControl,
		Param_Control3,
		Param_FootControl,
		Param_PortamentoTime,
		Param_Data,
		Param_ChannelVolume,
		Param_Balance,
		Param_Control9,
		Param_Panorama,
		Param_Expression,
		Param_Control12,
		Param_Control13,
		Param_Control14,
		Param_Control15,
		Param_Control16,
		Param_Osc1Shape,
		Param_Osc1PW,
		Param_Osc1Wave,
		Param_Osc1Semitone,
		Param_Osc1Keyfollow,
		Param_Osc2Shape,
		Param_Osc2PW,
		Param_Osc2Wave,
		Param_Osc2Semitone,
		Param_Osc2Detune,
		Param_Osc2FMAmount,
		Param_Osc2Sync,
		Param_Osc2FltEnvAmt,
		Param_FMFiltEnvAmt,
		Param_Osc2Keyfollow,
		Param_PerfControl_BankSelect,
		Param_OscBalance,
		Param_SubOscVolume,
		Param_SubOscShape,
		Param_OscMainVolume,
		Param_NoiseVolume,
		Param_RingModMVolume,
		Param_NoiseColor,
		Param_FilterCutA,
		Param_FilterCutB,
		Param_FilterResA,
		Param_FilterResB,
		Param_FilterEnvAmtA,
		Param_FilterEnvAmtB,
		Param_FilterKeyFollowA,
		Param_FilterKeyFollowB,
		Param_FilterBalance,
		Param_SaturationCurve,
		Param_FilterModeA,
		Param_FilterModeB,
		Param_FilterRouting,
		Param_FilterEnvAttack,
		Param_FilterEnvDecay,
		Param_FilterEnvSustain,
		Param_FilterEnvSustainTime,
		Param_FilterEnvRelease,
		Param_AmpEnvAttack,
		Param_AmpEnvDecay,
		Param_AmpEnvSustain,
		Param_AmpEnvSustainTime,
		Param_AmpEnvRelease,
		Param_HoldPedal,
		Param_PortamentoPedal,
		Param_SostenutoPedal,
		Param_Lfo1Rate,
		Param_Lfo1Shape,
		Param_Lfo1EnvMode,
		Param_Lfo1Mode,
		Param_Lfo1Symmetry,
		Param_Lfo1Keyfollow,
		Param_Lfo1KeyTrigger,
		Param_Osc1Lfo1Amount,
		Param_Osc2Lfo1Amount,
		Param_PWLfo1Amount,
		Param_ResoLfo1Amount,
		Param_FltGainLfo1Amount,
		Param_Lfo2Rate,
		Param_Lfo2Shape,
		Param_Lfo2EnvMode,
		Param_Lfo2Mode,
		Param_Lfo2Symmetry,
		Param_Lfo2Keyfollow,
		Param_Lfo2Keytrigger,
		Param_OscShapeLfo2Amount,
		Param_FmAmountLfo2Amount,
		Param_Cutoff1Lfo2Amount,
		Param_Cutoff2Lfo2Amount,
		Param_PanoramaLfo2Amount,
		Param_PatchVolume,
		Param_Transpose,
		Param_KeyMode,
		Param_UnisonMode,
		Param_UnisonDetune,
		Param_UnisonPanSpread,
		Param_UnisonLfoPhase,
		Param_InputMode,
		Param_InputSelect,
		Param_ChorusMix,
		Param_ChorusRate,
		Param_ChorusDepth,
		Param_ChorusDelay,
		Param_ChorusFeedback,
		Param_ChorusLfoShape,
		Param_DelayReverMode,
		Param_EffectSend,
		Param_DelayTime,
		Param_DelayFeedback,
		Param_DelayRateReverbDecayTime,
		Param_DelayDepthReverbRoomSize,
		Param_DelayLfoShape,
		Param_DelayColor,
		Param_KeybLocal,
		Param_AllNotesOff,

		Param_ArpMode,
		Param_ArpPatternSelect,
		Param_ArpOctaveRange,
		Param_ArpHoldEnable,
		Param_ArpNoteLength,
		Param_ArpSwing,

		Param_Lfo3Rate,
		Param_Lfo3Shape,
		Param_Lfo3Mode,
		Param_Lfo3Keyfollow,
		Param_Lfo3Destination,
		Param_OscLfo3Amount,
		Param_Lfo3FadeInTime,
		Param_ClockTempo,
		Param_ArpClock,
		Param_Lfo1Clock,
		Param_Lfo2Clock,
		Param_DelayClock,
		Param_Lfo3Clock,
		Param_ControlSmoothMode,
		Param_BenderRangeUp,
		Param_BenderRangeDown,
		Param_BenderScale,
		Param_Filter1EnvPolarity,
		Param_Filter2EnvPolarity,
		Param_Filter2CutoffLink,
		Param_FilterKeytrackBase,
		Param_OscFMMode,
		Param_OscInitPhase,
		Param_PunchIntensity,
		Param_InputFollowMode,
		Param_VocoderMode,
		Param_Osc3Mode,
		Param_Osc3Volume,
		Param_Osc3Semitone,
		Param_Osc3Detune,
		Param_LowEqFreq,
		Param_HighEqFreq,
		Param_Osc1ShapeVelocity,
		Param_Osc2ShapeVelocity,
		Param_PulseWidthVelocity,
		Param_FmAmountVelocity,
		Param_SoftKnob1ShortName,
		Param_SoftKnob2ShortName,
		Param_Filter1EnvAmtVelocity,
		Param_Filter2EnvAmtVelocity,
		Param_Resonance1Velocity,
		Param_Resonance2Velocity,
		Param_SecondOutputBalance,
		Param_AmpVelocity,
		Param_PanoramaVelocity,
		Param_SoftKnob1Single,
		Param_SoftKnob2Single,
		Param_Assign1Source,
		Param_Assign1Destination,
		Param_Assign1Amount,
		Param_Assign2Source,
		Param_Assign2Destination1,
		Param_Assign2Amount1,
		Param_Assign2Destination2,
		Param_Assign2Amount2,
		Param_Assign3Source,
		Param_Assign3Destination1,
		Param_Assign3Amount1,
		Param_Assign3Destination2,
		Param_Assign3Amount2,
		Param_Assign2Destination3,
		Param_Assign2Amount3,
		Param_Lfo1AssignDest,
		Param_Lfo1AssignAmount,
		Param_Lfo2AssignDest,
		Param_Lfo2AssignAmount,
		Param_PhaserMode,
		Param_PhaserMix,
		Param_PhaserRate,
		Param_PhaserDepth,
		Param_PhaserFreq,
		Param_PhaserFeedback,
		Param_PhaserSpread,
		Param_MidEqGain,
		Param_MidEqFreq,
		Param_MidEqQFactor,
		Param_LowEqGain,
		Param_HighEqGain,
		Param_BassIntensity,
		Param_BassTune,
		Param_InputRingMod,
		Param_DistortionCurve,
		Param_DistortionIntensity,
		Param_Assign4Source,
		Param_Assign4Destination,
		Param_Assign4Amount,
		Param_Assign5Source,
		Param_Assign5Destination,
		Param_Assign5Amount,
		Param_Assign6Source,
		Param_Assign6Destination,
		Param_Assign6Amount,
		Param_FilterSelect,

		Param_DelayOutputSelect,
		Param_PartBankSelect,
		Param_PartBankChange,
		Param_PartProgramChange,
		Param_PartMidiChannel,
		Param_PartLowKey,
		Param_PartHighKey,
		Param_PartTranspose,
		Param_PartDetune,
		Param_PartVolume,
		Param_PartMidiVolumeInit,
		Param_PartOutputSelect,
		Param_GlobalSecondOutputSelect,
		Param_GlobalKeybTransposeButtons,
		Param_GlobalKeybLocal,
		Param_GlobalKeybMode,
		Param_GlobalKeybTranspose,
		Param_GlobalKeyModWheelControl,
		Param_GlobalKeybPedal1Control,
		Param_GlobalKeybPedal2Control,
		Param_GlobalKeybPressureSensitive,
		Param_PartEnable,
		Param_PartMidiVolumeEnable,
		Param_PartHoldPedalEnable,
		Param_KeybToMidi,
		Param_NoteStealPriority,
		Param_PartProgChangeEnable,
		Param_GlobalProgChangeEnable,
		Param_MultiProgChangeEnable,
		Param_GlobalMidiVolumeEnable,
		Param_InputThruLevel,
		Param_InputBoost,
		Param_MasterTune,
		Param_DeviceID,
		Param_MidiControlLowPage,
		Param_MidiControlHighPage,
		Param_MidiArpSend,
		Param_KnobDisplay,
		Param_MidiDumpTX,
		Param_MidiDumpRX,
		Param_MultiProgramChange,
		Param_MidiClockRX,
		Param_SoftKnob1Mode,
		Param_SoftKnob2Mode,
		Param_SoftKnob1Global,
		Param_SoftKnob2Global,
		Param_SoftKnob1Midi,
		Param_SoftKnob2Midi,
		Param_ExpertMode,
		Param_KnobMode,
		Param_MemoryProtect,
		Param_SoftThru,
		Param_PanelDestination,
		Param_PlayMode,
		Param_PartNumber,
		Param_GlobalChannel,
		Param_LedMode,
		Param_LcdContrast,
		Param_MasterVolume,

			// UNDEFINED / UNUSED / STUBS
		Param_PageA_Undefined92,
		Param_PageA_Undefined95,
		Param_PageA_Undefined96,
		Param_PageA_Undefined111,
		Param_PageA_Undefined120,
		Param_PageA_Undefined121,
		Param_PageA_Undefined124,
		Param_PageA_Undefined125,
		Param_PageA_Undefined126,
		Param_PageA_Undefined127,
		Param_PageB_Undefined0,
		Param_PageB_Undefined14,
		Param_PageB_Undefined15,
		Param_PageB_Undefined22,
		Param_PageB_Undefined23,
		Param_PageB_Undefined24,
		Param_PageB_Undefined29,
		Param_PageB_Undefined37,
		Param_PageB_Undefined40,
		Param_PageB_Undefined53,
		Param_PageB_Undefined69,
		Param_PageB_Undefined83,
		Param_PageB_Undefined91,
		Param_PageB_Undefined111,
		Param_PageB_Undefined123,
		Param_PageB_Undefined124,
		Param_PageC_Undefined0,
		Param_PageC_Undefined1,
		Param_PageC_Undefined2,
		Param_PageC_Undefined3,
		Param_PageC_Undefined4,
		Param_PageC_Undefined15,
		Param_PageC_Undefined16,
		Param_PageC_Undefined17,
		Param_PageC_Undefined18,
		Param_PageC_Undefined19,
		Param_PageC_Undefined20,
		Param_PageC_Undefined21,
		Param_PageC_Undefined23,
		Param_PageC_Undefined24,
		Param_PageC_Undefined25,
		Param_PageC_Undefined26,
		Param_PageC_Undefined27,
		Param_PageC_Undefined28,
		Param_PageC_Undefined29,
		Param_PageC_Undefined30,
		Param_PageC_Undefined76,
		Param_PageC_Undefined79,
		Param_PageC_Undefined80,
		Param_PageC_Undefined81,
		Param_PageC_Undefined82,
		Param_PageC_Undefined83,
		Param_PageC_Undefined84,
		Param_PageC_Undefined88,
		Param_PageC_Undefined89,
		Param_PageC_Undefined100,
		Param_PageC_Undefined101,
		Param_PageC_Undefined102,
		Param_PageC_Undefined103,
		Param_PageC_Undefined104,
		Param_PageC_Undefined119,
		// Text Chars / Unused
		Param_PageB_Undefined110,
		Param_PageB_Undefined111_,
		Param_PageB_Undefined112,
		Param_PageB_Undefined113,
		Param_PageB_Undefined114,
		Param_PageB_Undefined115,
		Param_PageB_Undefined116,
		Param_PageB_Undefined117,
		Param_PageB_Undefined118,
		Param_PageB_Undefined119,
		Param_PageB_Undefined120,
		Param_PageB_Undefined121,
		Param_PageC_Undefined5,
		Param_PageC_Undefined6,
		Param_PageC_Undefined7,
		Param_PageC_Undefined8,
		Param_PageC_Undefined9,
		Param_PageC_Undefined10,
		Param_PageC_Undefined11,
		Param_PageC_Undefined12,
		Param_PageC_Undefined13,
		Param_PageC_Undefined14,

		Param_Count
	};

    using SysEx = std::vector<uint8_t>;
    class Controller : private juce::Timer
    {
    public:
        friend Parameter;
        static constexpr auto kNameLength = 10;

        Controller(AudioPluginAudioProcessor &, unsigned char deviceId = 0x00);

        // this is called by the plug-in on audio thread!
        void dispatchVirusOut(const std::vector<synthLib::SMidiEvent> &);

        void printMessage(const SysEx &) const;

        // currently Value as I figure out what's the best approach
        // ch - [0-15]
        // bank - [0-2] (ABC)
        // paramIndex - [0-127]
        juce::Value *getParamValue(uint8_t ch, uint8_t bank, uint8_t paramIndex);
        juce::Value *getParamValue(ParameterType _param);

        // bank - 0-1 (AB)
        juce::StringArray getSinglePresetNames(int bank) const;
        juce::StringArray getMultiPresetsName() const;
		bool isMultiMode() { return getParamValue(0, 2, 0x7a)->getValue(); }
		// part 0 - 15 (ignored when single! 0x40...)
		void setCurrentPartPreset(uint8_t part, uint8_t bank, uint8_t prg);
		uint8_t getCurrentPartBank(uint8_t part);
		uint8_t getCurrentPartProgram(uint8_t part);
		juce::String getCurrentPartPresetName(uint8_t part);
		uint32_t getBankCount() const { return static_cast<uint32_t>(m_singles.size()); }
		void parseMessage(const SysEx &);
		void sendSysEx(const SysEx &);
	private:
		void timerCallback() override;
        static constexpr size_t kDataSizeInBytes = 256; // same for multi and single

        struct MultiPatch
        {
            uint8_t bankNumber;
            uint8_t progNumber;
            uint8_t data[kDataSizeInBytes];
        };

        struct SinglePatch
        {
            uint8_t bankNumber;
            uint8_t progNumber;
            uint8_t data[kDataSizeInBytes];
        };

        MultiPatch m_multis[128]; // RAM has 128 Multi 'snapshots'
        std::array<std::array<SinglePatch, 128>, 8> m_singles;

        static const std::initializer_list<Parameter::Description> m_paramsDescription;

        struct ParamIndex
        {
            uint8_t page;
            uint8_t partNum;
            uint8_t paramNum;
            bool operator<(ParamIndex const &rhs) const
            {
				if (page < rhs.page)         return false;
				if (page > rhs.page)         return true;
				if (partNum < rhs.partNum)   return false;
				if (partNum > rhs.partNum)   return true;
				if (paramNum < rhs.paramNum) return false;
				if (paramNum > rhs.paramNum) return true;
				return false;
			}
        };

		std::map<ParamIndex, std::unique_ptr<Parameter>> m_synthInternalParams;
		std::map<ParamIndex, Parameter *> m_synthParams; // exposed and managed by audio processor
		std::map<ParameterType, ParamIndex> m_paramTypeToParamIndex;

		void registerParams();
		// tries to find synth param in both internal and host.
		// @return found parameter or nullptr if none found.
		Parameter *findSynthParam(uint8_t _part, uint8_t _page, uint8_t _paramIndex);
		Parameter* findSynthParam(const ParamIndex& _paramIndex);

		// unchecked copy for patch data bytes
        static inline uint8_t copyData(const SysEx &src, int startPos, uint8_t *dst);

        template <typename T> juce::String parseAsciiText(const T &, int startPos) const;
        void parseSingle(const SysEx &);
        void parseMulti(const SysEx &);
        void parseParamChange(const SysEx &);
        void parseControllerDump(synthLib::SMidiEvent &);

        std::vector<uint8_t> constructMessage(SysEx msg);

        AudioPluginAudioProcessor &m_processor;
        juce::CriticalSection m_eventQueueLock;
        std::vector<synthLib::SMidiEvent> m_virusOut;
        unsigned char m_deviceId;
        uint8_t m_currentBank[16];
        uint8_t m_currentProgram[16];
    };
}; // namespace Virus
