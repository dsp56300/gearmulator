#pragma once

#include "88lib/gs/efx.h"
#include "88lib/gs/params.h"
#include "88lib/gs/partState.h"
#include "88lib/gs/toneCatalog.h"

#include "juceRmlUi/rmlInterfaces.h"

#include "juceUiLib/messageBox.h"

#include "synthLib/midiTypes.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <memory>
#include <string>
#include <vector>

namespace Rml { class Element; class ElementFormControlInput; class Event; }
namespace juceRmlUi { class DragSource; class ElemComboBox; class Menu; class RmlComponent; }

namespace emu88Player
{
	class Editor;
	class Processor;

	// The part editor: a mixer page with every part's volume and pan beside the system and effect parameters, and a
	// page per part with its parameters beside the instrument and variation, picked from the tone lists in the
	// board's own ROMs. Edits go to the board as they are made; what the window shows is the processor's part state,
	// which follows every message the board receives, so a song or a live input moves the controls too. Send writes
	// the whole state to the board, Receive reads it back with data requests.
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
		// A mixer slider moved, or reset
		void setMixerValue(size_t _part, bool _volume, uint8_t _value);
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
		// A slider being dragged: refresh leaves it where the mouse has it
		Rml::Element* m_dragging = nullptr;
		bool m_updatingUi = false;

		// In the order they were made, the latest last: a tone map has to arrive before the tone it applies to
		struct PendingValue
		{
			uint8_t port;
			uint32_t address;
			std::vector<uint8_t> data;
		};
		std::vector<PendingValue> m_pending;

		bool m_receiving = false;
		std::string m_status;
		bool m_statusError = false;
	};
}
