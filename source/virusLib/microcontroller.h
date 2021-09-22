#pragma once

#include "../dsp56300/source/dsp56kEmu/dsp.h"
#include "../dsp56300/source/dsp56kEmu/hdi08.h"

#include "romfile.h"

#include "../synthLib/deviceTypes.h"
#include "../synthLib/midiTypes.h"

#include <list>
#include <mutex>

namespace virusLib
{
class Microcontroller
{
public:
	enum SysexMessageType : uint8_t
	{
		DUMP_SINGLE                = 0x10,
		DUMP_MULTI                 = 0x11,
		REQUEST_SINGLE             = 0x30,
		REQUEST_MULTI              = 0x31,
		REQUEST_BANK_SINGLE        = 0x32,
		REQUEST_BANK_MULTI         = 0x33,
		REQUEST_ARRANGEMENT        = 0x34,			// current Multi + all referenced Singles in the Multi. Only the Singles whose parts are enabled in the Multi are sent
		REQUEST_GLOBAL             = 0x35,			// All parameters not related to a single or a multi
		REQUEST_TOTAL              = 0x36,			// Single RAM Banks + Multi RAM Bank + Global
		REQUEST_CONTROLLER_DUMP    = 0x37,			// Sends the current Single edit buffer as individual controller/polypressure/sysex

		PARAM_CHANGE_A             = 0x70,
		PARAM_CHANGE_B             = 0x71,
		PARAM_CHANGE_C             = 0x72,
	};

    enum ControlCommand : uint8_t
	{
		AUDITION					= 01,

    	// Multi
    	MULTI_NAME_CHAR_0			= 5,
    	MULTI_NAME_CHAR_1			= 6,
    	MULTI_NAME_CHAR_2			= 7,
    	MULTI_NAME_CHAR_3			= 8,
    	MULTI_NAME_CHAR_4			= 9,
    	MULTI_NAME_CHAR_5			= 10,
    	MULTI_NAME_CHAR_6			= 11,
    	MULTI_NAME_CHAR_7			= 12,
    	MULTI_NAME_CHAR_8			= 13,
    	MULTI_NAME_CHAR_9			= 14,

    	MULTI_DELAY_OUTPUT_SELECT	= 22,

		UNK1a						= 26,
		UNK1b						= 27,
		UNK1c						= 28,
		UNK1d						= 29,

    	// Multi parts
    	PART_BANK_SELECT			= 31,		// The change is executed once a program change is received
		PART_BANK_CHANGE			= 32,		// The change is executed immediately
		PART_PROGRAM_CHANGE			= 33,
		PART_MIDI_CHANNEL			= 34,
		PART_LOW_KEY				= 35,
		PART_HIGH_KEY				= 36,
		PART_TRANSPOSE				= 37,
    	PART_DETUNE					= 38,
		PART_VOLUME					= 39,
		PART_MIDI_VOLUME_INIT		= 40,
		PART_OUTPUT_SELECT			= 41,

		SECOND_OUTPUT_SELECT		= 45,

    	UNK35						= 53,
		UNK36						= 54,

    	// Multi parts
    	PART_ENABLE					= 72,
		PART_MIDI_VOLUME_ENABLE		= 73,
    	PART_HOLD_PEDAL_ENABLE		= 74,
    	PART_KEYB_TO_MIDI			= 75,
    	UNK76						= 76,
    	PART_NOTE_STEAL_PRIO		= 77,
    	PART_PROG_CHANGE_ENABLE		= 78,

    	INPUT_THRU_LEVEL			= 90,
		INPUT_BOOST					= 91,
		MASTER_TUNE					= 92,
		DEVICE_ID					= 93,
		MIDI_CONTROL_LOW_PAGE		= 94,
		MIDI_CONTROL_HIGH_PAGE		= 95,
		MIDI_ARPEGGIATOR_SEND		= 96,
		MULTI_PROGRAM_CHANGE		= 105,
		MIDI_CLOCK_RX				= 106,
		UNK6d						= 109,
		PLAY_MODE					= 122,
		PART_NUMBER					= 123,
		GLOBAL_CHANNEL				= 124,
		LED_MODE					= 125,
		LCD_CONTRAST				= 126,
		PANEL_DESTINATION			= 121,
		MASTER_VOLUME				= 127
	};

	enum MultiDump : uint8_t
	{
		MD_NAME_CHAR_0				= 4,
		MD_NAME_CHAR_1				= 5,
		MD_NAME_CHAR_2				= 6,
		MD_NAME_CHAR_3				= 7,
		MD_NAME_CHAR_4				= 8,
		MD_NAME_CHAR_5				= 9,
		MD_NAME_CHAR_6				= 10,
		MD_NAME_CHAR_7				= 11,
		MD_NAME_CHAR_8				= 12,
		MD_NAME_CHAR_9				= 13,

		MD_CLOCK_TEMPO				= 15,
		MD_DELAY_MODE				= 16,
		MD_DELAY_TIME				= 17,
		MD_DELAY_FEEDBACK			= 18,
		MD_DELAY_RATE				= 19,
		MD_DELAY_DEPTH				= 20,
		MD_DELAY_SHAPE				= 21,
		MD_DELAY_OUTPUT_SELECT		= 22,
		MD_DELAY_CLOCK				= 23,
		MD_DELAY_COLOR				= 24,

		MD_PART_BANK_NUMBER			= 32,
		MD_PART_PROGRAM_NUMBER		= 48,
		MD_PART_MIDI_CHANNEL		= 64,
		MD_PART_LOW_KEY				= 80,
		MD_PART_HIGH_KEY			= 96,
		MD_PART_TRANSPOSE			= 112,
		MD_PART_DETUNE				= 128,
		MD_PART_VOLUME				= 144,
		MD_PART_MIDI_VOLUME_INIT	= 160,
		MD_PART_OUTPUT_SELECT		= 176,
		MD_PART_EFFECT_SEND			= 192,
		MD_PART_STATE				= 240,
	};

	enum MultiPartStateBits : uint8_t
	{
		MD_PART_ENABLE,
		MD_PART_MIDI_VOLUME_ENABLE,
		MD_PART_HOLD_PEDAL_ENABLE,
		MD_PART_KEYB_TO_MIDI,
		MD_PART_INTERNAL_BIT4,
		MD_PART_NOTE_STEAL_PRIORITY,
		MD_PART_PROG_CHANGE_ENABLE
	};

	enum Page
	{
		PAGE_A = 0x70,
		PAGE_B = 0x71,
		PAGE_C = 0x72,
	};

	enum ProgramType
	{
		SINGLE = 0x40
	};

	enum PlayMode
	{
		PlayModeSingle,
		PlayModeMultiSingle,	// This one is just a different view of multi mode on the hardware, not needed
		PlayModeMulti,
	};

	const uint8_t OMNI_DEVICE_ID = 0x10;

	using TPreset = ROMFile::TPreset;

	explicit Microcontroller(dsp56k::HDI08& hdi08, ROMFile& romFile);

	bool sendMIDI(const synthLib::SMidiEvent& _ev, bool cancelIfFull = false);
	bool sendSysex(const std::vector<uint8_t>& _data, bool _cancelIfFull, std::vector<synthLib::SMidiEvent>& _responses);

	bool writeSingle(uint8_t _bank, uint8_t _program, const TPreset& _data);
	bool writeMulti(uint8_t _bank, uint8_t _program, const TPreset& _data);
	bool requestMulti(uint8_t _bank, uint8_t _program, TPreset& _data) const;
	bool requestSingle(uint8_t _bank, uint8_t _program, TPreset& _data) const;

	void sendInitControlCommands();

	void createDefaultState();
	void process(size_t _size);

	bool getState(std::vector<unsigned char>& _state, synthLib::StateType _type);
	bool setState(const std::vector<unsigned char>& _state, synthLib::StateType _type);

	bool sendMIDItoDSP(uint8_t _a, uint8_t _b, uint8_t _c, bool cancelIfFull) const;

	void sendPendingMidiEvents(uint32_t _maxOffset);

private:
	bool send(Page page, uint8_t part, uint8_t param, uint8_t value, bool cancelIfFull = false);
	void sendControlCommand(ControlCommand command, uint8_t value);
	bool sendPreset(uint8_t program, const std::vector<dsp56k::TWord>& preset, bool isMulti = false);
	bool needsToWaitForHostBits(char flag1,char flag2) const;
	void writeHostBitsWithWait(char flag1,char flag2) const;
	void waitUntilReady() const;
	void waitUntilBufferEmpty() const;
	static std::vector<dsp56k::TWord> presetToDSPWords(const TPreset& _preset);
	bool getSingle(uint32_t _bank, uint32_t _preset, TPreset& _result) const;

	bool partBankSelect(uint8_t _part, uint8_t _value, bool _immediatelySelectSingle);
	bool partProgramChange(uint8_t _part, uint8_t _value);
	bool multiProgramChange(uint8_t _value);
	bool loadMulti(uint8_t _program, const TPreset& _multi);
	bool loadMultiSingle(uint8_t _part);
	bool loadMultiSingle(uint8_t _part, const TPreset& _multi);

	void applyToSingleEditBuffer(Page _page, uint8_t _part, uint8_t _param, uint8_t _value);
	static void applyToSingleEditBuffer(TPreset& _single, Page _page, uint8_t _param, uint8_t _value);
	void applyToMultiEditBuffer(uint8_t _part, uint8_t _param, uint8_t _value);
	
	dsp56k::HDI08& m_hdi08;
	ROMFile& m_rom;

	TPreset m_multiEditBuffer;

	std::array<uint8_t, 256> m_globalSettings;
	std::vector<std::vector<TPreset>> m_singles;

	// Multi mode
	std::array<TPreset,16> m_singleEditBuffers;

	// Single mode
	TPreset m_singleEditBuffer;
	uint8_t m_currentBank = 0;
	uint8_t m_currentSingle = 0;

	// Device does not like if we send everything at once, therefore we delay the send of Singles after sending a Multi
	struct SPendingPresetWrite
	{
		uint8_t program = 0;
		bool isMulti = false;
		std::vector<dsp56k::TWord> data;
	};

	std::list<SPendingPresetWrite> m_pendingPresetWrites;

	dsp56k::RingBuffer<synthLib::SMidiEvent, 1024, false> m_pendingMidiEvents;
	mutable std::recursive_mutex m_mutex;
};

}
