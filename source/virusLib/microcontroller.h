#pragma once

#include "romfile.h"

#include "../synthLib/deviceTypes.h"
#include "../synthLib/midiTypes.h"
#include "../synthLib/buildconfig.h"

#include <list>
#include <mutex>

#include "hdi08List.h"
#include "hdi08MidiQueue.h"
#include "hdi08TxParser.h"
#include "microcontrollerTypes.h"

namespace virusLib
{
	class DspSingle;
	class DemoPlayback;

class Microcontroller
{
	friend class DemoPlayback;
public:
	using TPreset = ROMFile::TPreset;

	explicit Microcontroller(DspSingle& _dsp, const ROMFile& romFile, bool _useEsaiBasedMidiTiming);

	bool sendMIDI(const synthLib::SMidiEvent& _ev);
	bool sendSysex(const std::vector<uint8_t>& _data, std::vector<synthLib::SMidiEvent>& _responses, synthLib::MidiEventSource _source);

	bool writeSingle(BankNumber _bank, uint8_t _program, const TPreset& _data);
	bool writeMulti(BankNumber _bank, uint8_t _program, const TPreset& _data);
	bool requestMulti(BankNumber _bank, uint8_t _program, TPreset& _data);
	bool requestSingle(BankNumber _bank, uint8_t _program, TPreset& _data);

	void sendInitControlCommands();

	void createDefaultState();
	void process(size_t _size);

#if !SYNTHLIB_DEMO_MODE
	bool getState(std::vector<unsigned char>& _state, synthLib::StateType _type);
	bool setState(const std::vector<unsigned char>& _state, synthLib::StateType _type);
	bool setState(const std::vector<synthLib::SMidiEvent>& _events);
#endif

	void addDSP(DspSingle& _dsp, bool _useEsaiBasedMidiTiming);

	void readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut);
	void sendPendingMidiEvents(uint32_t _maxOffset);
	Hdi08MidiQueue& getMidiQueue(size_t _index) { return m_midiQueues[_index]; }

	static PresetVersion getPresetVersion(const TPreset& _preset);
	static PresetVersion getPresetVersion(uint8_t _versionCode);

	static uint8_t calcChecksum(const std::vector<uint8_t>& _data, const size_t _offset);

	bool dspHasBooted() const;

	const ROMFile& getROM() const { return m_rom; }

private:
	bool send(Page page, uint8_t part, uint8_t param, uint8_t value);
	void sendControlCommand(ControlCommand command, uint8_t value);
	bool sendPreset(uint8_t program, const TPreset& _data, bool isMulti = false);
	void writeHostBitsWithWait(uint8_t flag0, uint8_t flag1);
	std::vector<dsp56k::TWord> presetToDSPWords(const TPreset& _preset, bool _isMulti) const;
	bool getSingle(BankNumber _bank, uint32_t _preset, TPreset& _result) const;

	bool partBankSelect(uint8_t _part, uint8_t _value, bool _immediatelySelectSingle);
	bool partProgramChange(uint8_t _part, uint8_t _value);
	bool multiProgramChange(uint8_t _value);
	bool loadMulti(uint8_t _program, const TPreset& _multi);
	bool loadMultiSingle(uint8_t _part);
	bool loadMultiSingle(uint8_t _part, const TPreset& _multi);

	void applyToSingleEditBuffer(Page _page, uint8_t _part, uint8_t _param, uint8_t _value);
	static void applyToSingleEditBuffer(TPreset& _single, Page _page, uint8_t _param, uint8_t _value);
	void applyToMultiEditBuffer(uint8_t _part, uint8_t _param, uint8_t _value);
	Page globalSettingsPage() const;
	bool isPageSupported(Page _page) const;
	void processHdi08Tx(std::vector<synthLib::SMidiEvent>& _midiEvents);
	bool waitingForPresetReceiveConfirmation() const;
	void receiveUpgradedPreset();

	static bool isValid(const TPreset& _preset);

	Hdi08List m_hdi08;
	std::vector<Hdi08TxParser> m_hdi08TxParsers;
	std::vector<Hdi08MidiQueue> m_midiQueues;

	const ROMFile& m_rom;

	std::array<TPreset,128> m_multis{};
	TPreset m_multiEditBuffer;

	std::array<uint32_t, 256> m_globalSettings;
	std::vector<std::vector<TPreset>> m_singles;

	// Multi mode
	std::array<TPreset,16> m_singleEditBuffers{};

	// Single mode
	TPreset m_singleEditBuffer{};
	uint8_t m_currentBank = 0;
	uint8_t m_currentSingle = 0;

	uint8_t m_sentPresetProgram = 0;
	bool m_sentPresetIsMulti = false;

	// Device does not like if we send everything at once, therefore we delay the send of Singles after sending a Multi
	struct SPendingPresetWrite
	{
		uint8_t program = 0;
		bool isMulti = false;
		TPreset data;
	};

	std::list<SPendingPresetWrite> m_pendingPresetWrites;

	std::vector<std::pair<synthLib::MidiEventSource, std::vector<uint8_t>>> m_pendingSysexInput;
	std::vector<synthLib::SMidiEvent> m_midiOutput;

	mutable std::recursive_mutex m_mutex;
	bool m_loadingState = false;
};

}
