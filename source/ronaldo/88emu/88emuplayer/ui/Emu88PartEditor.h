#pragma once

#include "88lib/gs/efx.h"
#include "88lib/gs/params.h"
#include "88lib/gs/partState.h"
#include "88lib/gs/toneCatalog.h"
#include "88lib/mt32/memory.h"
#include "88lib/mt32/params.h"
#include "88lib/mt32/presets.h"

#include "juceRmlUi/rmlInterfaces.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/midiTypes.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <array>
#include <deque>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Rml { class Element; class ElementFormControlInput; class Event; }
namespace juceRmlUi { class DragSource; class ElemComboBox; class Menu; class RmlComponent; }

namespace emu88Player
{
	class Editor;
	class Processor;

	// Whether a board has the part editor: the Sound Canvases and the boards that run their programs, and the boards
	// of the MT-32 dialect, which have the mixer only
	bool hasPartEditor(emu88Lib::DeviceModel _model);

	// The part editor: a mixer page with every part's volume and pan beside the system and effect parameters, and a
	// page per part with its parameters beside the instrument and variation, picked from the tone lists in the
	// board's own ROMs. Edits go to the board as they are made; what the window shows is the processor's part state,
	// which follows every message the board receives, so a song or a live input moves the controls too. Send writes
	// the whole state to the board, Receive reads it back with data requests.
	//
	// The boards of the MT-32 dialect, the MT-32, CM-32L, CM-32P and CM-64, show the parts of their LA board and of
	// their PCM board, from the processor's copy of their memory, named from their ROMs: the mixer beside the system
	// areas, and a page per part with its patch temp, an LA part's timbre with its four partials too, beside the
	// timbres or tones it can play.
	class PartEditorWindow final : public juce::DocumentWindow, juce::Timer
	{
	public:
		PartEditorWindow(Editor& _owner, Processor& _processor);
		~PartEditorWindow() override;

		void closeButtonPressed() override;

		// The board changed: rebuild for its parts and tone lists
		void deviceChanged();

	private:
		static constexpr int g_mixerPage = -1;
		// The lights of the parts that receive MIDI look every second timer tick, and a part stays lit for two looks
		// after its last message: at most 15 redraws a second for them, whatever plays
		static constexpr uint8_t g_activityInterval = 2;
		static constexpr uint8_t g_activityLooks = 2;

		struct MixerRow
		{
			Rml::Element* row = nullptr;
			Rml::Element* solo = nullptr;
			Rml::Element* mute = nullptr;
			Rml::Element* tone = nullptr;
			Rml::ElementFormControlInput* volume = nullptr;
			Rml::ElementFormControlInput* pan = nullptr;
			Rml::Element* volumeValue = nullptr;
			Rml::Element* panValue = nullptr;
			Rml::Element* light = nullptr;
		};

		// A parameter of gs::globalParams() or gs::partParams(): a slider with its value, or a choice
		struct Control
		{
			bool global = false;
			size_t param = 0;
			// One of the insertion effect's twenty parameters: what the type shown has there
			const emu88Lib::gs::EfxParam* efx = nullptr;
			Rml::ElementFormControlInput* slider = nullptr;
			juceRmlUi::ElemComboBox* combo = nullptr;
			Rml::Element* value = nullptr;
		};

		void timerCallback() override;

		void build();
		void buildMixer();
		void buildGroupAndPartTabs();
		void labelPartTabs();
		// Lights up the tabs and mixer rows of the parts that received MIDI since the last look, a bit per part, and
		// lets the others go out: whether a light changed
		bool updateActivity(uint64_t _parts);
		void refreshActivity();
		// The board keeps the activity only while the window shows: whether a light went out
		bool enableActivity(bool _enabled);
		void buildControls();
		// The rows of the insertion effect type's parameters, for the type the setup has
		void buildEfxControls();
		// Finds a row's elements in _container and wires them
		Control makeControl(Rml::Element* _container, bool _global, size_t _param, const emu88Lib::gs::EfxParam* _efx);
		void wireActions();

		void selectPage(int _page);
		// The SC-8850's switch of the tab strip between part groups A and B and groups C and D
		void selectBank(uint8_t _bank);
		void selectPart(size_t _part);
		size_t selectedPart() const;
		void openPartMenu(size_t _part, Rml::Event& _event);

		// What a MIDI file holds: the part shown, the system and effects, or everything after a GS Reset
		enum class Export : uint8_t { Part, Globals, All };

		// Send and receive, for the part shown and for all of them, and the MIDI files, to save or drag to a host
		void openActionsPanel();
		void closeActionsPanel();
		bool isActionsPanelOpen() const;
		void saveMidiFile(Export _export);
		std::string exportName(Export _export) const;
		std::vector<uint8_t> createMidiFile(Export _export) const;
		// Written to the temporary folder for a drag; empty when it could not be
		std::string writeExportFile(Export _export) const;
		void setPartType(size_t _part, uint8_t _useForRhythm);
		void toggleSolo(size_t _part);
		void toggleMute(size_t _part);
		// Mutes the parts the solo leaves out and unmutes those it no longer does
		void applySolo();
		void setRxNoteMessage(size_t _part, bool _on);
		bool matchesFilter(const std::string& _name) const;

		void refresh();
		void refreshMixer();
		void refreshTabs();
		void refreshPart();
		void refreshMapSelect();
		void refreshActions();
		// The part's for the part controls, nullptr for the global ones
		void refreshControls(std::vector<Control>& _controls, const emu88Lib::gs::Part* _part);
		void buildToneLists();
		// Marks the row of the part's tone without building the list again, which would lose its scroll position
		void applyListSelection();
		void showStatus(const std::string& _text, bool _error = false);
		std::string toneName(size_t _part) const;

		// Queues a data set; the timer sends what is queued, the last value of each address only
		void setValue(uint8_t _port, uint32_t _address, const std::vector<uint8_t>& _data);
		void setPartValue(size_t _part, uint32_t _address, const std::vector<uint8_t>& _data);
		void flushPending();
		void setGlobalParam(size_t _param, uint16_t _value);
		void setPartParam(size_t _part, size_t _param, uint16_t _value);
		// What a double-click goes back to: the value after a GS Reset, or what the insertion effect type sets
		uint16_t defaultValue(bool _global, size_t _param, const emu88Lib::gs::EfxParam* _efx) const;
		void resetControl(bool _global, size_t _param, const emu88Lib::gs::EfxParam* _efx);
		// A mixer slider moved, or reset. A row is a part: of a group on the Sound Canvases, of the LA or the PCM
		// board on the MT-32 dialect's boards, see m_laParts. Values are the board's.
		void setMixerValue(size_t _row, bool _volume, uint8_t _value);
		uint8_t mixerValue(size_t _row, bool _volume) const;
		uint8_t mixerDefault(size_t _row, bool _volume) const;
		uint8_t mixerMax(size_t _row, bool _volume) const;
		// A value as its slider has it: the LA and PCM boards' pan runs from the right
		uint8_t mixerSlider(size_t _row, bool _volume, uint8_t _value) const;
		std::string mixerText(size_t _row, bool _volume, uint8_t _value) const;
		std::string mixerLabel(size_t _row) const;
		// The heading before a row that starts a group of parts, empty for none
		std::string mixerHeading(size_t _row) const;

		// A part of the MT-32 dialect's boards: the LA board's parts 1-8 and its rhythm part, the CM-32P's parts 1-6
		struct LaPart
		{
			emu88Lib::mt32::Board board;
			uint8_t part;
		};
		// Where a part's volume or pan is
		emu88Lib::mt32::Location laMixerLocation(const LaPart& _part, bool _volume) const;
		bool isLaRhythm(size_t _row) const;
		// The timbre or tone a part plays
		std::string laPartName(const LaPart& _part) const;
		// A DT1 for the board, queued with the edits
		void setLaValue(uint32_t _address, const std::vector<uint8_t>& _data);
		// Bytes for the board's memory: into the copy shown at once, and queued for the board
		void writeLa(emu88Lib::mt32::Area _area, size_t _entry, uint16_t _offset, const std::vector<uint8_t>& _data);

		// A setting of the LA board's or the PCM board's memory: of a system area, of a part's patch temp, or of one
		// of the rhythm part's keys. The system areas fill the side pane on those boards, the others a part's page.
		struct LaControl
		{
			emu88Lib::mt32::Area area;
			size_t entry;
			// The parameter list it is of, see emu88Lib::mt32::params(), and where that block starts in the entry: a
			// partial's place in a timbre
			emu88Lib::mt32::Block block;
			uint16_t base;
			size_t param;
			Rml::ElementFormControlInput* slider = nullptr;
			juceRmlUi::ElemComboBox* combo = nullptr;
			Rml::Element* value = nullptr;
			// A number to drag, or buttons a value each
			Rml::Element* number = nullptr;
			std::vector<std::pair<Rml::Element*, uint8_t>> segments;
		};
		// How a parameter's row asks for its value: as the parameter suggests, a slider, a list or buttons
		enum class LaInput : uint8_t { Auto, Slider, Choice, Segments };
		std::string laParamRow(emu88Lib::mt32::Area _area, size_t _entry, emu88Lib::mt32::Block _block, uint16_t _base,
			const char* _id, LaInput _input = LaInput::Auto, const char* _label = nullptr, bool _applies = true) const;
		// Numbers to drag under small headings, in one row: an envelope's times or levels
		std::string laNumberGrid(emu88Lib::mt32::Area _area, size_t _entry, uint16_t _base, const char* _label,
			const std::vector<std::pair<const char*, const char*>>& _cells) const;
		// The rows of an entry's parameters in a section, without the parameters in _skip, or only those in _only, and
		// the controls in them wired up once they are in the document
		std::string laControlRows(emu88Lib::mt32::Area _area, size_t _entry, emu88Lib::mt32::Block _block,
			uint16_t _base, const std::string& _section, const std::vector<std::string>& _skip = {},
			const std::vector<std::string>& _only = {}) const;
		void wireLaControls(Rml::Element* _container, emu88Lib::mt32::Area _area, size_t _entry,
			emu88Lib::mt32::Block _block, uint16_t _base, std::vector<LaControl>& _controls);
		void buildLaControls();
		void refreshLaControls(std::vector<LaControl>& _controls);
		void setLaParam(emu88Lib::mt32::Area _area, size_t _entry, emu88Lib::mt32::Block _block, uint16_t _base,
			size_t _param, uint8_t _value);
		// What a double-click goes back to: for a timbre temp the timbre it was copied from, else the power-on state
		uint8_t laDefault(emu88Lib::mt32::Area _area, size_t _entry, uint16_t _offset) const;

		// The timbre a part's page edits: a melodic part's timbre temp, or the timbre memory one that a rhythm key
		// plays; a key playing one of the ROM has none
		struct LaTimbre
		{
			emu88Lib::mt32::Area area = emu88Lib::mt32::Area::Count;
			size_t entry = 0;
			bool valid = false;
		};
		LaTimbre laEditedTimbre() const;
		void refreshLaTimbreEditor();
		// Whether a partial of the timbre plays a PCM sample, by its pair's structure
		bool laPartialIsPcm(const LaTimbre& _timbre, uint8_t _partial) const;
		// Its structure value with the partial's kind changed, the pair's mix kept where it can be
		uint8_t laStructureWith(const LaTimbre& _timbre, uint8_t _partial, bool _pcm) const;
		void refreshLaEnvelopes();
		// Up and down in a part's list: the instrument, variation, timbre or tone before or after the one played
		void moveListSelection(int _delta);
		// The memory timbres the rhythm keys play
		std::vector<uint8_t> laRhythmMemoryTimbres() const;
		// The next edit queued for the board, once the last one had its room
		void sendNextLaValue();

		// A part's page: its patch temp's settings, or the rhythm part's level and keys, beside the list of what it
		// can play
		void buildLaPartControls();
		void refreshLaPart();
		// What the list offers, and the selection in it
		struct LaListRow
		{
			bool heading = false;
			uint8_t group = 0;		// timbre group, tone media; for a patch or a rhythm timbre 0
			uint8_t number = 0;
			std::string label;
			std::string name;
		};
		std::vector<LaListRow> laListRows() const;
		// The group and number of the row that is what the part plays, 0xff for none
		std::pair<uint8_t, uint8_t> laListSelection() const;
		void selectLaListRow(const LaListRow& _row);
		void applyLaListSelection();
		// A timbre by group and number, a rhythm key's timbre by its value, a patch or a tone, by name
		std::string laTimbreName(uint8_t _group, uint8_t _number) const;
		std::string laToneName(uint8_t _media, uint8_t _number) const;
		// "Part 1", "Rhythm Part", on the CM-64 "LA Part 1" and "PCM Part 1"
		std::string laPartTitle(size_t _row) const;

		// The MT-32 dialect's transfers: a part's settings, the system areas, everything as a restore, which starts
		// with an all parameters reset. The boards need a moment after that reset, the CM-32P another after its
		// first patch memory write, and the MT-32 1.x some room between messages, so they go out paced, from the
		// timer, and a MIDI file lays them out the same way.
		std::vector<emu88Lib::mt32::Message> laPartMessages(size_t _row) const;
		std::vector<emu88Lib::mt32::Message> laSystemMessages() const;
		// The room a board needs after a message, in microseconds and in timer ticks
		uint32_t laPause(const emu88Lib::mt32::Message& _message, bool& _pcmPatchMemorySeen) const;
		uint32_t laPauseTicks(const emu88Lib::mt32::Message& _message, bool& _pcmPatchMemorySeen) const;
		void laSend(std::vector<emu88Lib::mt32::Message> _messages, const std::string& _done);
		bool laSending() const { return !m_laSendQueue.empty(); }
		// Data requests for the LA board, which answers them; the CM-32P answers none
		std::vector<std::pair<uint32_t, uint32_t>> laPartRanges(size_t _row) const;
		std::vector<std::pair<uint32_t, uint32_t>> laAllRanges() const;
		void laReceive(const std::vector<std::pair<uint32_t, uint32_t>>& _ranges);

		// Audition: a note on the part a list item was picked for, held while the mouse button is. The rhythm
		// part's is the key shown, a GS drum part's a snare.
		void startAudition(uint8_t _port, uint8_t _channel, uint8_t _note);
		void stopAudition();
		void auditionPart(size_t _part);
		// The parts by channel for the processor's activity, from the memory shown
		void updateLaListeners();
		// The controller, RPN or NRPN that sets a part parameter, on the part's channel: it reaches every part on
		// that channel. False when the part receives no channel or the parameter has none.
		bool channelMessages(size_t _part, size_t _param, uint16_t _value,
			std::vector<synthLib::SMidiEvent>& _events) const;
		void selectTone(size_t _part, uint8_t _bankMsb, uint8_t _program);
		void selectToneMap(size_t _part, uint8_t _map);
		// A program change on the part's channel, with bank select and map: the SC-8850 takes a new map with nothing
		// else. False when the part receives no channel.
		bool programChange(size_t _part, std::vector<synthLib::SMidiEvent>& _events) const;

		// The messages that set parts, and the system and effects, to what the setup has: one group per setting, whose
		// messages belong together, an RPN with its data entry
		using MessageGroups = std::vector<std::vector<synthLib::SMidiEvent>>;
		MessageGroups setupMessages(size_t _first, size_t _count, bool _withGlobals) const;
		void sendParts(size_t _first, size_t _count, bool _withGlobals);
		void receiveParts(size_t _first, size_t _count, bool _withGlobals);

		Editor& m_owner;
		Processor& m_processor;
		juceRmlUi::RmlInterfaces m_interfaces;
		std::unique_ptr<juceRmlUi::RmlComponent> m_rml;

		emu88Lib::DeviceModel m_model;
		std::shared_ptr<const emu88Lib::gs::ToneCatalog> m_catalog;
		std::string m_catalogError;
		emu88Lib::gs::Setup m_setup;
		uint64_t m_revision = 0;

		// The MT-32 dialect's boards: their memory as the processor follows it, and as a reset leaves it, which a
		// double-click goes back to. They have no switch for solo and mute. A page is one of m_laParts.
		bool m_laDialect = false;
		std::vector<LaPart> m_laParts;
		emu88Lib::mt32::Memory m_laMemory;
		emu88Lib::mt32::Memory m_laDefaults;
		uint64_t m_laRevision = 0;
		std::shared_ptr<const emu88Lib::mt32::PresetCatalog> m_laPresets;
		std::vector<LaControl> m_laControls;
		std::vector<LaControl> m_laPartControls;
		// What a part's controls were built for: the part, and the rhythm key shown
		std::string m_laControlsKey;
		size_t m_laRhythmKey = 0;
		std::vector<LaListRow> m_laListRows;
		std::vector<Rml::Element*> m_laListElements;
		struct LaQueued
		{
			emu88Lib::mt32::Message message;
			uint32_t pauseTicks;
		};
		std::deque<LaQueued> m_laSendQueue;
		uint32_t m_laSendWait = 0;
		size_t m_laSendTotal = 0;
		std::string m_laSendDone;

		// Whether picking from a list plays a note, from the config, and the note playing
		bool m_audition = true;
		struct AuditionNote
		{
			bool active = false;
			uint8_t port = 0;
			uint8_t channel = 0;
			uint8_t note = 0;
		};
		AuditionNote m_auditionNote;
		std::vector<std::string> m_laKeyNames;
		juceRmlUi::ElemComboBox* m_laKeySelect = nullptr;
		// The timbre editor: the tab shown, 0 the common settings and 1-4 a partial's, the name field, the partials'
		// switches and the tabs, the waveform's buttons and the envelopes' graphs
		uint8_t m_laTab = 0;
		Rml::ElementFormControlInput* m_laNameInput = nullptr;
		std::array<Rml::Element*, 4> m_laMuteButtons{};
		std::array<Rml::Element*, 4> m_laPlayChecks{};
		std::array<Rml::Element*, 5> m_laTabs{};
		std::array<Rml::Element*, 2> m_laTypeButtons{};
		std::array<Rml::Element*, 2> m_laShapeButtons{};
		std::array<Rml::Element*, 2> m_laBankButtons{};
		std::array<Rml::Element*, 3> m_laEnvelopeLines{};
		std::array<Rml::Element*, 3> m_laEnvelopeGrids{};
		// A number being dragged: what it sets, from where and what it was
		struct LaNumberDrag
		{
			bool active = false;
			LaControl control{};
			float startY = 0.0f;
			int startValue = 0;
			int max = 0;
		};
		LaNumberDrag m_laNumberDrag;
		void dragLaNumber(float _y);
		// The part page changed under a click of its own and comes again from the timer, and whether it may come now
		bool m_laRebuildPending = false;
		bool m_laBuildNow = false;
		// Timer ticks until the next edit may go out
		uint32_t m_laFlushWait = 0;

		int m_page = g_mixerPage;
		// The part to go back to from the mixer
		size_t m_lastPart = 0;
		// The part shown: its group, and m_page its place in the group
		uint8_t m_group = 0;
		// The two part groups the tabs show, by the first, 0 or 2: the SC-8850 has four
		uint8_t m_bank = 0;
		uint8_t m_groupCount = 1;
		// What the list shows: names that contain it, any case
		std::string m_filter;

		// The rows of the list, to mark the selection in
		struct ListRow
		{
			Rml::Element* element = nullptr;
			uint8_t program = 0;
			uint8_t bankMsb = 0;
			bool instrument = false;
		};
		std::vector<ListRow> m_listRows;
		std::string m_listSelection;

		// Solo is the board's own mute, Rx Note Message (40 1x 08), of every part it leaves out: these are the parts
		// soloed and the ones the solo muted, which it unmutes again, a bit per part
		uint64_t m_soloed = 0;
		uint64_t m_soloMuted = 0;
		std::shared_ptr<juceRmlUi::Menu> m_menu;
		// The grips of the MIDI files in the actions panel
		std::vector<std::unique_ptr<juceRmlUi::DragSource>> m_exportSources;
		std::unique_ptr<genericUI::MessageBox::FileDialog> m_fileDialog;

		// What the lists were built from, so that a refresh only rebuilds them when that changes
		std::string m_listKey;

		Rml::Element* m_root = nullptr;
		std::vector<MixerRow> m_mixerRows;
		std::vector<Control> m_globalControls;
		std::vector<Control> m_partControls;
		std::vector<Control> m_efxControls;
		// The insertion effect type the parameter rows are for, -1 for none
		int m_efxShownType = -1;
		// The board after a GS Reset, what a double-click resets a control to
		emu88Lib::gs::Setup m_defaults;
		juceRmlUi::ElemComboBox* m_mapSelect = nullptr;
		std::vector<std::string> m_mapNames;
		std::vector<Rml::Element*> m_groupTabs;
		std::vector<Rml::Element*> m_partTabs;
		// The lights of the tabs shown
		std::vector<Rml::Element*> m_partLights;
		// Per part, the looks it stays lit
		std::array<uint8_t, 64> m_activity{};
		bool m_activityEnabled = false;
		uint8_t m_activityTick = 0;
		// A slider being dragged: refresh leaves it where the mouse has it
		Rml::Element* m_dragging = nullptr;
		bool m_updatingUi = false;

		// In the order they were made, the latest last: a tone map has to arrive before the tone it applies to
		struct PendingValue
		{
			uint8_t port;
			uint32_t address;
			std::vector<uint8_t> data;
			// A DT1 of the MT-32 dialect rather than of GS
			bool mt32 = false;
		};
		std::vector<PendingValue> m_pending;

		bool m_receiving = false;
		std::string m_status;
		bool m_statusError = false;
	};
}
