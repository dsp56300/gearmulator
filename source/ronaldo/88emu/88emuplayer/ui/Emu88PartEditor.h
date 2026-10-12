#pragma once

#include "88lib/gs/partState.h"
#include "88lib/gs/toneCatalog.h"

#include "juceRmlUi/rmlInterfaces.h"

#include "synthLib/midiTypes.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include <memory>
#include <string>
#include <vector>

namespace Rml { class Element; class ElementFormControlInput; class Event; }
namespace juceRmlUi { class Menu; class RmlComponent; }

namespace emu88Player
{
	class Editor;
	class Processor;

	// The part editor: a mixer page with every part's volume and pan, and a page per part that picks its
	// instrument and variation from the tone lists in the board's own ROMs. Edits go to the board as they are made;
	// what the window shows is the processor's part state, which follows every message the board receives, so a
	// song or a live input moves the controls too. Send writes the whole state to the board, Receive reads it back
	// with data requests.
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

		void timerCallback() override;

		void build();
		void buildMixer();
		void buildGroupAndPartTabs();
		void wireActions();

		void selectPage(int _page);
		void selectGroup(uint8_t _group);
		void selectPart(size_t _part);
		size_t selectedPart() const;
		void openPartMenu(int _page, Rml::Event& _event);
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
		void refreshActions();
		void buildToneLists();
		// Marks the row of the part's tone without building the list again, which would lose its scroll position
		void applyListSelection();
		void showStatus(const std::string& _text, bool _error = false);
		std::string toneName(size_t _part) const;

		// Queues a data set for a part; the timer sends what is queued, the last value of each address only
		void setPartValue(size_t _part, uint32_t _address, const std::vector<uint8_t>& _data);
		void flushPending();
		void selectTone(size_t _part, uint8_t _bankMsb, uint8_t _program);
		void selectToneMap(size_t _part, uint8_t _map);
		// A program change on the part's channel, with bank select and map: the SC-8850 takes a new map with nothing
		// else. False when the part receives no channel.
		bool programChange(size_t _part, std::vector<synthLib::SMidiEvent>& _events) const;

		void sendParts(size_t _first, size_t _count, bool _withMasterVolume);
		void receiveParts(size_t _first, size_t _count, bool _withMasterVolume);

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
		// The part page to go back to from the mixer
		int m_lastPartPage = 0;
		uint8_t m_group = 0;
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

		// What the lists were built from, so that a refresh only rebuilds them when that changes
		std::string m_listKey;

		Rml::Element* m_root = nullptr;
		std::vector<MixerRow> m_mixerRows;
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
