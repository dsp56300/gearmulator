#include "88emuplayer/ui/Emu88PartEditor.h"

#include "88emuplayer/Emu88Processor.h"
#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"

#include "88lib/gs/gs.h"
#include "88lib/gs/midiClip.h"
#include "88lib/mt32/params.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlComponentConfig.h"
#include "juceRmlUi/rmlDragData.h"
#include "juceRmlUi/rmlDragSource.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlElemStrokes.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlMenu.h"
#include "juceRmlUi/rmlPressSwallower.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ScrollTypes.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <sstream>

namespace emu88Player
{
	using namespace editor;
	namespace gs = emu88Lib::gs;

	namespace
	{
		// A flag that is set for as long as the scope lasts
		struct ScopedFlag
		{
			explicit ScopedFlag(bool& _flag) : flag(_flag), was(_flag) { flag = true; }
			~ScopedFlag() { flag = was; }
			bool& flag;
			bool was;
		};

		std::string number(const int _value, const int _digits = 3)
		{
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%0*d", _digits, _value);
			return buf;
		}

		std::string panText(const uint8_t _pan)
		{
			if(_pan == 0)
				return "Rnd";
			if(_pan == 64)
				return "C";
			return _pan < 64 ? "L" + std::to_string(64 - _pan) : "R" + std::to_string(_pan - 64);
		}

		std::string groupLetter(const uint8_t _group)
		{
			return std::string(1, static_cast<char>('A' + _group));
		}

		std::string encode(const std::string& _text)
		{
			return Rml::StringUtilities::EncodeRml(_text);
		}

		void setText(Rml::Element* _element, const std::string& _text)
		{
			if(_element && _element->GetInnerRML() != encode(_text))
				_element->SetInnerRML(encode(_text));
		}

		void setActionEnabled(Rml::Element* _element, const bool _enabled)
		{
			if(_element)
				juceRmlUi::helper::setEnabled(_element, _enabled);
		}

		bool isActionEnabled(const Rml::Element* _element)
		{
			return _element && !_element->IsPseudoClassSet("disabled");
		}

		std::string lowercase(std::string _text)
		{
			std::transform(_text.begin(), _text.end(), _text.begin(),
				[](const unsigned char _c) { return static_cast<char>(std::tolower(_c)); });
			return _text;
		}

		void addClick(Rml::Element* _element, const std::function<void()>& _callback)
		{
			if(_element)
				juceRmlUi::EventListener::AddClick(_element, _callback);
		}

		bool isSc8850(const emu88Lib::DeviceModel _model)
		{
			return _model == emu88Lib::DeviceModel::Sc8850 || _model == emu88Lib::DeviceModel::Sc8820;
		}

		// The part page's sections in two columns: the mixer, pitch and setup on the left
		int sectionColumn(const char* _section)
		{
			return std::strcmp(_section, "Tone Modify") == 0 || std::strcmp(_section, "Scale Tuning") == 0 ? 1 : 0;
		}

		// A global parameter's data set: the master tune is four nibbles, the insertion effect type MSB and LSB
		std::vector<uint8_t> globalData(const gs::Param& _param, const uint16_t _value)
		{
			if(_param.size == 4)
			{
				return {static_cast<uint8_t>(_value >> 12 & 0x0f), static_cast<uint8_t>(_value >> 8 & 0x0f),
					static_cast<uint8_t>(_value >> 4 & 0x0f), static_cast<uint8_t>(_value & 0x0f)};
			}
			if(_param.size == 2)
				return {static_cast<uint8_t>(_value >> 8 & 0x7f), static_cast<uint8_t>(_value & 0x7f)};
			return {static_cast<uint8_t>(_value)};
		}

		std::string controlId(const bool _global, const size_t _param)
		{
			return std::string(_global ? "g" : "p") + std::to_string(_param);
		}

		// A row of the parameters: the label, then a choice, or a slider with its value
		std::string controlRow(const std::string& _id, const std::string& _label, const bool _choice, const int _min,
			const int _max, const bool _channelOnly, const std::string& _note = {})
		{
			std::ostringstream rml;
			rml << "<div class=\"peRow" << (_channelOnly ? " channelOnly" : "") << "\"";
			if(_channelOnly)
				rml << " title=\"Set with a channel message, on the part's MIDI channel. "
				       "The device cannot be asked for it.\"";
			rml << "><div id=\"peLbl" << _id << "\" class=\"peRowLabel\" title=\""
			    << encode(_note.empty() ? "Double-click for the default" : _note + " Double-click for the default.")
			    << "\">" << encode(_label) << "</div>";
			if(_choice)
				rml << "<combo id=\"peCtl" << _id << "\" class=\"peCombo\" popup=\"window\"><combotext/></combo>";
			else
				rml << "<input id=\"peCtl" << _id << "\" class=\"peSlider\" type=\"range\" min=\"" << _min
					<< "\" max=\"" << _max
				    << "\" step=\"1\" value=\"" << _min << "\"/><div id=\"peVal" << _id << "\" class=\"peRowValue\"/>";
			rml << "</div>";
			return rml.str();
		}

		std::vector<juceRmlUi::ElemComboBox::Entry> comboEntries(const std::vector<std::pair<uint16_t,
			std::string>>& _choices)
		{
			std::vector<juceRmlUi::ElemComboBox::Entry> entries;
			for(const auto& [value, text] : _choices)
				entries.push_back({text, value});
			return entries;
		}

		// A MIDI file that a grip of the actions panel hands to the host it is dragged to, written when the drag leaves
		// the window
		struct ExportDragData final : juceRmlUi::DragData
		{
			std::function<std::string()> write;

			bool canExportAsFiles() const override { return true; }

			bool getFilesForExport(std::vector<std::string>& _files, bool& _filesAreTemporary) override
			{
				const auto file = write ? write() : std::string();
				if(file.empty())
					return false;
				_files.push_back(file);
				_filesAreTemporary = true;
				return true;
			}
		};

		class ExportDragSource final : public juceRmlUi::DragSource
		{
		public:
			ExportDragSource(Rml::Element* _element, std::function<std::string()> _write)
				: DragSource(_element), m_write(std::move(_write))
			{
			}

			std::unique_ptr<juceRmlUi::DragData> createDragData() override
			{
				auto data = std::make_unique<ExportDragData>();
				data->write = m_write;
				return data;
			}

		private:
			std::function<std::string()> m_write;
		};

		// A group of messages as a single track has it, which plays on one port, A: part group B by its 50 addresses,
		// which reach it from there. What only port B's channels reach - a part's expression and bend range, the
		// SC-8850's map program change - and the SC-8850's part groups C and D are out of its reach: none for those.
		std::optional<std::vector<synthLib::SMidiEvent>> forPortA(std::vector<synthLib::SMidiEvent> _group)
		{
			for(auto& e : _group)
			{
				if(e.port == 0)
					continue;
				gs::Dt1 dt1;
				if(e.port > 1 || !gs::parseDt1(e.sysex.data(), e.sysex.size(), dt1) || dt1.address >> 16 != 0x40)
					return {};
				const auto message = gs::dt1(0x500000 | (dt1.address & 0xffff), std::vector<uint8_t>(dt1.data, dt1.data + dt1.size));
				e.sysex.assign(message.begin(), message.end());
				e.port = 0;
			}
			return _group;
		}

		// An event as the bytes on the wire: a program change and a channel pressure take one data byte
		std::vector<uint8_t> messageBytes(const synthLib::SMidiEvent& _event)
		{
			if(!_event.sysex.empty())
				return {_event.sysex.begin(), _event.sysex.end()};
			const auto type = _event.a & 0xf0;
			if(type == 0xc0 || type == 0xd0)
				return {_event.a, _event.b};
			return {_event.a, _event.b, _event.c};
		}

		std::vector<std::pair<uint16_t, std::string>> efxChoices(const gs::EfxParam& _param)
		{
			std::vector<std::pair<uint16_t, std::string>> choices;
			const auto list = gs::efxValueList(_param);
			for(size_t i = 0; i < list.size(); ++i)
				choices.emplace_back(static_cast<uint16_t>(_param.min + i), list[i]);
			return choices;
		}

		// A part parameter's data set: the fine tune comes with its LSB
		std::vector<uint8_t> partData(const gs::Param& _param, const gs::Part& _part, const uint16_t _value)
		{
			if(_param.size == 2)
				return {static_cast<uint8_t>(_value), _part.fineTuneLsb};
			return {static_cast<uint8_t>(_value)};
		}
	}

	bool hasPartEditor(const emu88Lib::DeviceModel _model)
	{
		return gs::isPartEditorModel(_model) || emu88Lib::mt32::isMt32Model(_model);
	}

	PartEditorWindow::PartEditorWindow(Editor& _owner, Processor& _processor)
		: juce::DocumentWindow("Part Editor", juce::Colours::black, juce::DocumentWindow::closeButton),
		  m_owner(_owner), m_processor(_processor), m_interfaces(_owner), m_model(_processor.deviceModel())
	{
		setUsingNativeTitleBar(true);
		setResizable(false, false);

		juceRmlUi::RmlComponentConfig config;
		config.refreshRateLimitHz = 30;
		config.includeDefaultTemplates = false;
		const auto software = m_processor.config().getIntValue("forceSoftwareRenderer", -1);
		if(software >= 0)
			config.forceSoftwareRenderer = software > 0 ? juceRmlUi::SoftwareRendererMode::ForceOn :
			                                             juceRmlUi::SoftwareRendererMode::ForceOff;
		m_rml = std::make_unique<juceRmlUi::RmlComponent>(m_interfaces, m_owner, "emu88PartEditor.rml", 1.0f,
			[](juceRmlUi::RmlComponent&, Rml::Context&) {},
			[](juceRmlUi::RmlComponent&, Rml::Context&) {}, config);
		setContentNonOwned(m_rml.get(), false);
		setContentComponentSize(g_partEditorWidth, g_partEditorHeight);
		centreAroundComponent(&m_owner, getWidth(), getHeight());

		{
			juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
			m_root = m_rml->getDocument()->GetElementById("partEditorRoot");
			if(m_root)
			{
				wireActions();
				build();
			}
		}
		m_audition = m_processor.config().getBoolValue("partEditorAudition", true);
		startTimerHz(30);
	}

	PartEditorWindow::~PartEditorWindow()
	{
		stopTimer();
		stopAudition();
		enableActivity(false);
		// A solo lasts as long as the window: the parts it muted play again
		m_soloed = 0;
		applySolo();
		// The content goes while it is still in the window, see SettingsWindow::reloadContent()
		m_root = nullptr;
		m_rml.reset();
		clearContentComponent();
	}

	void PartEditorWindow::closeButtonPressed()
	{
		m_owner.showPartEditor(false);
	}

	void PartEditorWindow::deviceChanged()
	{
		if(!m_root)
			return;
		stopAudition();
		m_laSendQueue.clear();
		m_pending.clear();
		m_listKey.clear();
		m_soloed = m_soloMuted = 0;
		m_activity.fill(0);
		m_activityTick = 0;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
		build();
		m_rml->enqueueUpdate();
	}

	void PartEditorWindow::build()
	{
		const ScopedFlag buildNow(m_laBuildNow);
		m_model = m_processor.deviceModel();
		const bool supported = gs::isPartEditorModel(m_model);
		m_laDialect = emu88Lib::mt32::isMt32Model(m_model);
		m_groupCount = supported ? std::max<uint8_t>(1, emu88Lib::getDeviceProfile(m_model).groupCount) : 0;
		m_group = std::min<uint8_t>(m_group, std::max<uint8_t>(1, m_groupCount) - 1);
		m_setup = m_processor.partState().setup(m_revision);
		if(m_lastPart >= m_setup.parts.size())
			m_lastPart = 0;

		m_catalog = m_processor.toneCatalog();
		m_catalogError = m_processor.toneCatalogError();
		m_defaults = gs::createDefaultSetup(m_model);

		m_laParts.clear();
		if(m_laDialect)
		{
			m_laMemory = m_processor.laState().memory(m_laRevision);
			m_laDefaults = emu88Lib::mt32::Memory(m_model);
			m_laPresets = m_processor.laPresets();
			if(m_laMemory.hasLa())
			{
				for(uint8_t p = 0; p < emu88Lib::mt32::g_laParts; ++p)
					m_laParts.push_back({emu88Lib::mt32::Board::La, p});
			}
			if(m_laMemory.hasPcm())
			{
				for(uint8_t p = 0; p < emu88Lib::mt32::g_pcmParts; ++p)
					m_laParts.push_back({emu88Lib::mt32::Board::Pcm, p});
			}
			updateLaListeners();
		}
		// The MT-32 dialect's boards have a mixer only, as wide as the window
		m_root->SetClass("laDialect", m_laDialect);

		setName(juce::String(emu88Lib::getDeviceProfile(m_model).displayName) + " Part Editor");

		if(auto* title = m_root->GetElementById("pePaneTitle"))
			title->SetInnerRML(m_laDialect ? "System" : "System and Effects");

		buildGroupAndPartTabs();
		buildMixer();
		// Both fill the side pane and the part page: the other's controls are gone with what it showed
		m_laControls.clear();
		m_laPartControls.clear();
		m_laControlsKey.clear();
		m_laKeySelect = nullptr;
		m_laKeyNames.clear();
		m_mapNames.clear();
		if(m_laDialect)
		{
			m_partControls.clear();
			m_globalControls.clear();
			m_efxControls.clear();
			m_efxShownType = -1;
			buildLaControls();
		}
		else
		{
			buildControls();
		}
		selectPage(supported || m_laDialect ? m_page : g_mixerPage);

		const auto& laPresetsError = m_processor.laPresetsError();
		if(m_laDialect)
			showStatus(laPresetsError.empty() ? std::string()
			           : "Preset timbres and tones unavailable: " + laPresetsError, !laPresetsError.empty());
		else if(!supported)
			showStatus("The part editor works with the Sound Canvas boards, from the SC-55 to the SC-8850, and with "
			           "the MT-32, CM-32L, CM-32P and CM-64.", true);
		else if(!m_catalog)
			showStatus("Instrument names unavailable: " + m_catalogError, true);
		else
			showStatus({});
	}

	void PartEditorWindow::wireActions()
	{
		juceRmlUi::EventListener::AddClick(m_root->GetElementById("peTabMixer"), [this] { selectPage(g_mixerPage); });

		if(auto* menu = m_root->GetElementById("peActionsMenu"))
		{
			juceRmlUi::EventListener::AddClick(menu, [this, menu]
			{
				if(isActionsPanelOpen())
					closeActionsPanel();
				else if(isActionEnabled(menu))
					openActionsPanel();
			});
		}
		// A press anywhere else closes the panel, and is for the panel only, as with a menu: what is under the
		// pointer does not get it. A context-menu press goes through, so that it can open its menu.
		juceRmlUi::EventListener::Add(m_root->GetOwnerDocument(), Rml::EventId::Mousedown, [this](Rml::Event& _event)
		{
			if(!isActionsPanelOpen())
				return;
			const auto* panel = m_root->GetElementById("peActionsPanel");
			const auto* button = m_root->GetElementById("peActionsMenu");
			for(const auto* e = _event.GetTargetElement(); e; e = e->GetParentNode())
			{
				if(e == panel || e == button)
					return;
			}
			if(!juceRmlUi::helper::isContextMenu(_event))
			{
				_event.StopPropagation();
				juceRmlUi::swallowRestOfPress(m_root->GetOwnerDocument());
			}
			closeActionsPanel();
		}, true);

		m_mapSelect = dynamic_cast<juceRmlUi::ElemComboBox*>(m_root->GetElementById("peMapSelect"));
		if(m_mapSelect)
		{
			juceRmlUi::EventListener::Add(m_mapSelect, Rml::EventId::Change, [this](Rml::Event&)
			{
				const auto index = m_mapSelect->getSelectedIndex();
				// The MT-32 dialect's boards have no tone maps
				if(m_laDialect)
					return;
				if(m_updatingUi || !m_catalog || index < 0 || static_cast<size_t>(index) >= m_catalog->maps.size() ||
				   m_page == g_mixerPage)
					return;
				selectToneMap(selectedPart(), m_catalog->maps[static_cast<size_t>(index)].number);
			});
		}

		// Up and down browse a part's list while it, or the filter, has the focus; the note plays while the key is down
		const auto browse = [this](Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
			if(key != Rml::Input::KI_UP && key != Rml::Input::KI_DOWN)
				return;
			_event.StopPropagation();
			moveListSelection(key == Rml::Input::KI_UP ? -1 : 1);
		};
		const auto release = [this](Rml::Event& _event)
		{
			const auto key = juceRmlUi::helper::getKeyIdentifier(_event);
			if(key == Rml::Input::KI_UP || key == Rml::Input::KI_DOWN)
				stopAudition();
		};
		for(const char* id : {"peTones", "peFilter"})
		{
			if(auto* element = m_root->GetElementById(id))
			{
				juceRmlUi::EventListener::Add(element, Rml::EventId::Keydown, browse);
				juceRmlUi::EventListener::Add(element, Rml::EventId::Keyup, release);
			}
		}

		if(auto* filter = dynamic_cast<Rml::ElementFormControlInput*>(m_root->GetElementById("peFilter")))
		{
			juceRmlUi::EventListener::Add(filter, Rml::EventId::Change, [this, filter](Rml::Event&)
			{
				const auto text = lowercase(filter->GetValue());
				if(text == m_filter)
					return;
				m_filter = text;
				m_listKey.clear();
				if(m_page != g_mixerPage)
					refreshPart();
			});
		}

		// A slider lets go wherever the mouse button comes up, inside the window or not
		juceRmlUi::EventListener::Add(m_root, Rml::EventId::Mouseup, [this](Rml::Event&)
		{
			m_dragging = nullptr;
			m_laNumberDrag.active = false;
			stopAudition();
			// A setting the board did not take goes back to what it has
			if(m_laDialect)
			{
				refreshLaControls(m_laControls);
				refreshLaControls(m_laPartControls);
			}
		});

		juceRmlUi::EventListener::Add(m_root, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			if(juceRmlUi::helper::getKeyIdentifier(_event) == Rml::Input::KI_ESCAPE)
			{
				_event.StopPropagation();
				if(isActionsPanelOpen())
					closeActionsPanel();
				else
					m_owner.showPartEditor(false);
			}
		});
	}

	void PartEditorWindow::buildGroupAndPartTabs()
	{
		m_groupTabs.clear();
		m_partTabs.clear();

		auto* groups = m_root->GetElementById("peGroups");
		auto* parts = m_root->GetElementById("pePartTabs");
		if(!groups || !parts)
			return;

		// Two part groups at a time, A1-A16 and B1-B16; the SC-8850 switches between those and its C and D
		std::ostringstream rml;
		for(uint8_t b = 0; m_groupCount > 2 && b * 2 < m_groupCount; ++b)
		{
			const auto first = groupLetter(static_cast<uint8_t>(b * 2));
			const auto second = groupLetter(static_cast<uint8_t>(b * 2 + 1));
			rml << "<button id=\"peBank" << int(b) << "\" class=\"peGroup\" title=\"Part groups " << first << " and "
			    << second << "\">" << first << "/" << second << "</button>";
		}
		groups->SetInnerRML(rml.str());
		juceRmlUi::helper::setVisible(groups, m_groupCount > 2);

		// The tabs stay, the switch relabels them: a tab is a place in the strip, its part depends on m_bank. The
		// MT-32 dialect's boards have a tab per part of m_laParts, the CM-64's PCM parts apart from its LA ones.
		const auto shown = m_laDialect ? m_laParts.size()
		                               : static_cast<size_t>(std::min<uint8_t>(2, m_groupCount)) * gs::g_partsPerGroup;
		const bool bothBoards = m_laMemory.hasLa() && m_laMemory.hasPcm();
		const size_t groupStart = !m_laDialect ? gs::g_partsPerGroup : bothBoards ? emu88Lib::mt32::g_laParts : 0;
		rml.str({});
		for(size_t t = 0; t < shown; ++t)
		{
			rml << "<button id=\"pePartTab" << t << "\" class=\"pePartTab"
			    << (groupStart && t == groupStart ? " groupStart" : "") << "\"/>";
		}
		parts->SetInnerRML(rml.str());
		parts->SetClass("twoGroups", !m_laDialect && shown > gs::g_partsPerGroup);

		for(uint8_t b = 0; m_groupCount > 2 && b * 2 < m_groupCount; ++b)
		{
			auto* tab = groups->GetElementById("peBank" + std::to_string(b));
			m_groupTabs.push_back(tab);
			juceRmlUi::EventListener::AddClick(tab, [this, b] { selectBank(static_cast<uint8_t>(b * 2)); });
		}
		for(size_t t = 0; t < shown; ++t)
		{
			auto* tab = parts->GetElementById("pePartTab" + std::to_string(t));
			m_partTabs.push_back(tab);
			juceRmlUi::EventListener::AddClick(tab, [this, t]
			{
				selectPart(m_bank * size_t{gs::g_partsPerGroup} + t);
			});
			juceRmlUi::EventListener::Add(tab, Rml::EventId::Mousedown, [this, t](Rml::Event& _event)
			{
				if(!juceRmlUi::helper::isContextMenu(_event))
					return;
				_event.StopPropagation();
				openPartMenu(m_bank * size_t{gs::g_partsPerGroup} + t, _event);
			});
		}

		m_bank = m_groupCount > 2 ? static_cast<uint8_t>(m_group / 2 * 2) : 0;
		labelPartTabs();
	}

	void PartEditorWindow::labelPartTabs()
	{
		m_partLights.assign(m_partTabs.size(), nullptr);
		for(size_t t = 0; t < m_partTabs.size(); ++t)
		{
			if(!m_partTabs[t])
				continue;
			const auto group = static_cast<uint8_t>(m_bank + t / gs::g_partsPerGroup);
			const auto number = std::to_string(t % gs::g_partsPerGroup + 1);
			// The light that MIDI for the part turns on, over the whole tab
			auto label = m_groupCount > 1 ? groupLetter(group) + number : number;
			if(m_laDialect)
			{
				// The CM-64 says which board a part is on
				const auto& part = m_laParts[t];
				const bool both = m_laMemory.hasLa() && m_laMemory.hasPcm();
				const std::string board = !both ? "" : part.board == emu88Lib::mt32::Board::La ? "LA " : "PCM ";
				label = isLaRhythm(t) ? std::string("Rhythm") : board + std::to_string(part.part + 1);
			}
			m_partTabs[t]->SetInnerRML("<div class=\"pePartLight\"/>" + label);
			m_partLights[t] = m_partTabs[t]->GetFirstChild();
		}
	}

	bool PartEditorWindow::updateActivity(const uint64_t _parts)
	{
		bool changed = false;
		for(size_t i = 0; i < m_activity.size(); ++i)
		{
			auto& looks = m_activity[i];
			const bool lit = looks > 0;
			if((_parts >> i) & 1)
				looks = g_activityLooks;
			else if(looks)
				--looks;
			changed |= lit != (looks > 0);
		}
		if(changed)
			refreshActivity();
		return changed;
	}

	void PartEditorWindow::refreshActivity()
	{
		// A light only switches its visibility, which needs no layout, and its class is its own, so that nothing
		// else is styled again
		const auto light = [this](Rml::Element* _light, const size_t _part)
		{
			if(_light && _part < m_activity.size())
				_light->SetClass("on", m_activity[_part] > 0);
		};
		for(size_t t = 0; t < m_partLights.size(); ++t)
			light(m_partLights[t], static_cast<size_t>(m_bank) * gs::g_partsPerGroup + t);
		// The mixer's rows when it shows, the page switch catches up with them
		if(m_page != g_mixerPage)
			return;
		for(size_t i = 0; i < m_mixerRows.size(); ++i)
			light(m_mixerRows[i].light, i);
	}

	bool PartEditorWindow::enableActivity(const bool _enabled)
	{
		if(_enabled == m_activityEnabled)
			return false;
		m_activityEnabled = _enabled;
		m_processor.setPartActivityEnabled(_enabled);
		const bool lit = std::any_of(m_activity.begin(), m_activity.end(), [](const uint8_t _looks)
		{
			return _looks > 0;
		});
		m_activity.fill(0);
		if(lit)
			refreshActivity();
		return lit;
	}

	std::string PartEditorWindow::toneName(const size_t _part) const
	{
		if(_part >= m_setup.parts.size())
			return {};
		const auto& p = m_setup.parts[_part];
		const auto map = m_setup.effectiveToneMap(_part);

		if(p.isDrumPart())
		{
			const auto result = m_catalog ? m_catalog->selectDrumSet(map, p.program) : gs::ToneCatalog::DrumSetResult{};
			if(result.set)
				return result.set->name;
			switch(m_catalog ? result.selection : gs::Selection::Tone)
			{
			case gs::Selection::User: return "User drum set " + number(p.program + 1);
			case gs::Selection::Silent: return "No drum set";
			default: return "Drum set " + number(p.program + 1);
			}
		}

		const auto result = m_catalog ? m_catalog->selectTone(map, p.bankMsb, p.program, p.keepTone != 0)
		                              : gs::ToneCatalog::ToneResult{};
		if(result.tone)
			return result.tone->name;
		switch(m_catalog ? result.selection : gs::Selection::Tone)
		{
		case gs::Selection::User: return "User tone " + std::to_string(p.bankMsb) + "/" + number(p.program + 1);
		case gs::Selection::Silent: return "No instrument";
		default: return "Program " + number(p.program + 1) + ", bank " + std::to_string(p.bankMsb);
		}
	}

	void PartEditorWindow::buildMixer()
	{
		m_mixerRows.clear();
		auto* rows = m_root->GetElementById("peMixerRows");
		if(!rows)
			return;

		if(auto* heading = m_root->GetElementById("peMixHeadTone"))
		{
			const bool la = m_laDialect && m_laMemory.hasLa();
			const bool pcm = m_laDialect && m_laMemory.hasPcm();
			heading->SetInnerRML(!m_laDialect ? "Instrument" : la && pcm ? "Timbre or Tone" : la ? "Timbre" : "Tone");
		}

		const auto slider = [this](const char* _id, const size_t _row, const bool _volume)
		{
			std::ostringstream rml;
			rml << "<input id=\"" << _id << _row << "\" class=\"peSlider\" type=\"range\" min=\"0\" max=\""
			    << int(mixerMax(_row, _volume)) << "\" step=\"1\" value=\""
			    << int(mixerSlider(_row, _volume, mixerDefault(_row, _volume))) << "\"/>";
			return rml.str();
		};

		const auto count = m_laDialect ? m_laParts.size() : m_setup.parts.size();
		std::ostringstream rml;
		for(size_t i = 0; i < count; ++i)
		{
			if(const auto heading = mixerHeading(i); !heading.empty())
				rml << "<div class=\"peMixGroupHeading\">" << heading << "</div>";
			rml << "<div id=\"peMix" << i << "\" class=\"peMixRow\">"
			    << "<div id=\"peMixLight" << i << "\" class=\"peMixLight\"/>";
			// The MT-32 dialect's boards have no switch for them, and no part pages
			if(!m_laDialect)
			{
				rml << "<div class=\"peMixSoloMute\">"
				    << "<button id=\"peSolo" << i << "\" class=\"peMixSolo\" title=\"Solo\">S</button>"
				    << "<button id=\"peMute" << i << "\" class=\"peMixMute\" title=\"Mute\">M</button></div>"
				    << "<div id=\"peMixName" << i << "\" class=\"peMixName editable\" title=\"Edit this part\">";
			}
			else
			{
				rml << "<div id=\"peMixName" << i << "\" class=\"peMixName\">";
			}
			rml << "<div class=\"peMixPart\">" << mixerLabel(i) << "</div>"
			    << "<div id=\"peMixTone" << i << "\" class=\"peMixTone\"/></div>"
			    << "<div class=\"peMixVolume\">" << slider("peVol", i, true)
			    << "<div id=\"peVolValue" << i << "\" class=\"peMixValue\"/></div>"
			    << "<div class=\"peMixPan\">";
			// The LA board's rhythm part stores a pan but plays its keys with their own, from the rhythm setup
			if(m_laDialect && isLaRhythm(i))
				rml << "<div class=\"peMixNote\" title=\"Each key has its own pan, in the rhythm setup\">Per key</div>";
			else
				rml << slider("pePan", i, false) << "<div id=\"pePanValue" << i << "\" class=\"peMixValue\"/>";
			rml << "</div></div>";
		}
		rows->SetInnerRML(rml.str());

		auto* document = rows->GetOwnerDocument();
		for(size_t i = 0; i < count; ++i)
		{
			const auto id = std::to_string(i);
			MixerRow row;
			row.row = document->GetElementById("peMix" + id);
			row.solo = document->GetElementById("peSolo" + id);
			row.mute = document->GetElementById("peMute" + id);
			row.tone = document->GetElementById("peMixTone" + id);
			row.light = document->GetElementById("peMixLight" + id);
			row.volume = dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("peVol" + id));
			row.pan = dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("pePan" + id));
			row.volumeValue = document->GetElementById("peVolValue" + id);
			row.panValue = document->GetElementById("pePanValue" + id);

			addClick(row.solo, [this, i] { toggleSolo(i); });
			addClick(row.mute, [this, i] { toggleMute(i); });
			if(!m_laDialect)
				addClick(document->GetElementById("peMixName" + id), [this, i] { selectPart(i); });

			for(auto* slider : {row.volume, row.pan})
			{
				if(!slider)
					continue;
				const bool isVolume = slider == row.volume;
				juceRmlUi::EventListener::Add(slider, Rml::EventId::Mousedown, [this, slider](Rml::Event&)
				{
					m_dragging = slider;
				});
				juceRmlUi::EventListener::Add(slider, Rml::EventId::Change, [this, i, slider, isVolume](Rml::Event&)
				{
					if(m_updatingUi)
						return;
					const auto max = int(mixerMax(i, isVolume));
					const auto position = std::clamp(std::atoi(slider->GetValue().c_str()), 0, max);
					// The slider's place back to the board's value: the same mapping both ways
					setMixerValue(i, isVolume, mixerSlider(i, isVolume, static_cast<uint8_t>(position)));
				});
				juceRmlUi::EventListener::Add(slider, Rml::EventId::Dblclick, [this, i, isVolume](Rml::Event&)
				{
					m_dragging = nullptr;
					setMixerValue(i, isVolume, mixerDefault(i, isVolume));
					refreshMixer();
				});
			}
			m_mixerRows.push_back(row);
		}
	}

	bool PartEditorWindow::isLaRhythm(const size_t _row) const
	{
		const auto& part = m_laParts[_row];
		return part.board == emu88Lib::mt32::Board::La && part.part == emu88Lib::mt32::g_rhythmPart;
	}

	std::string PartEditorWindow::mixerLabel(const size_t _row) const
	{
		if(m_laDialect)
			return isLaRhythm(_row) ? "R" : std::to_string(m_laParts[_row].part + 1);
		const auto part = static_cast<int>(_row % gs::g_partsPerGroup) + 1;
		return m_groupCount > 1 ? groupLetter(static_cast<uint8_t>(_row / gs::g_partsPerGroup)) + number(part, 2)
		                        : std::to_string(part);
	}

	std::string PartEditorWindow::mixerHeading(const size_t _row) const
	{
		if(m_laDialect)
		{
			// The CM-64's two boards
			const auto& part = m_laParts[_row];
			if(part.part != 0 || !m_laMemory.hasLa() || !m_laMemory.hasPcm())
				return {};
			return part.board == emu88Lib::mt32::Board::La ? "LA parts" : "PCM parts";
		}
		if(m_groupCount > 1 && _row % gs::g_partsPerGroup == 0)
			return "Part group " + groupLetter(static_cast<uint8_t>(_row / gs::g_partsPerGroup));
		return {};
	}

	emu88Lib::mt32::Location PartEditorWindow::laMixerLocation(const LaPart& _part, const bool _volume) const
	{
		using namespace emu88Lib::mt32;
		if(_part.board == Board::Pcm)
			return {Area::PcmPatchTemp, _part.part, static_cast<uint16_t>(_volume ? 0x14 : 0x13)};
		return {Area::PatchTemp, _part.part, static_cast<uint16_t>(_volume ? 0x08 : 0x09)};
	}

	uint8_t PartEditorWindow::mixerValue(const size_t _row, const bool _volume) const
	{
		if(m_laDialect)
		{
			const auto location = laMixerLocation(m_laParts[_row], _volume);
			return m_laMemory.value(location.area, location.entry, location.offset);
		}
		const auto& p = m_setup.parts[_row];
		return _volume ? p.level : p.pan;
	}

	uint8_t PartEditorWindow::mixerDefault(const size_t _row, const bool _volume) const
	{
		if(m_laDialect)
		{
			const auto location = laMixerLocation(m_laParts[_row], _volume);
			return m_laDefaults.value(location.area, location.entry, location.offset);
		}
		if(_row >= m_defaults.parts.size())
			return _volume ? 100 : 64;
		const auto& p = m_defaults.parts[_row];
		return _volume ? p.level : p.pan;
	}

	uint8_t PartEditorWindow::mixerMax(const size_t _row, const bool _volume) const
	{
		// The LA and PCM boards' levels go to 100, their pans to 14 and 127
		if(!m_laDialect)
			return 127;
		if(_volume)
			return 100;
		return m_laParts[_row].board == emu88Lib::mt32::Board::La ? 14 : 127;
	}

	uint8_t PartEditorWindow::mixerSlider(const size_t _row, const bool _volume, const uint8_t _value) const
	{
		if(!m_laDialect || _volume)
			return _value;
		const auto max = mixerMax(_row, false);
		return static_cast<uint8_t>(max - std::min(_value, max));
	}

	std::string PartEditorWindow::mixerText(const size_t _row, const bool _volume, const uint8_t _value) const
	{
		if(_volume)
			return std::to_string(_value);
		if(!m_laDialect)
			return panText(_value);
		using namespace emu88Lib::mt32;
		const auto block = m_laParts[_row].board == Board::La ? Block::PatchTemp : Block::PcmPatchTemp;
		const auto index = findParamById(block, "Pan");
		return index >= 0 ? formatValue(params(block)[static_cast<size_t>(index)], _value) : std::to_string(_value);
	}

	namespace
	{
		// A name as the board keeps it: ten characters, filled with spaces
		std::string laName(const uint8_t* _data)
		{
			std::string text(10, ' ');
			for(size_t i = 0; i < 10; ++i)
			{
				if(_data[i] >= 0x20 && _data[i] < 0x7f)
					text[i] = static_cast<char>(_data[i]);
			}
			const auto end = text.find_last_not_of(' ');
			return end == std::string::npos ? std::string() : text.substr(0, end + 1);
		}

		std::string laTimbreCode(const uint8_t _group, const uint8_t _number)
		{
			char text[8];
			std::snprintf(text, sizeof(text), "%c%02d", "abir"[_group & 3], _number + 1);
			return text;
		}

		// The CM-32P's internal tones; a patch selecting one past them plays nothing
		constexpr uint8_t g_pcmInternalTones = 84;

		// Where a partial's parameters start in a timbre
		uint16_t laPartialBase(const uint8_t _partial)
		{
			return static_cast<uint16_t>(emu88Lib::mt32::g_timbreCommonSize + _partial * emu88Lib::mt32::g_partialSize);
		}

		// The pace of the timer, which sends the MT-32 dialect's SysEx with the room the boards need
		constexpr uint32_t g_tickMicroseconds = 1000000 / 30;
	}

	namespace
	{
		// A list to pick from where the list has every value, else a slider with its value
		bool isLaChoice(const emu88Lib::mt32::Param& _param, const emu88Lib::mt32::LaFirmware _firmware)
		{
			using emu88Lib::mt32::ParamText;
			switch(_param.text)
			{
			case ParamText::Number:
			case ParamText::Signed:
			case ParamText::PanLa:
			case ParamText::PanPcm:
			case ParamText::MasterTuneLa:
			case ParamText::MasterTunePcm:
			case ParamText::BiasLevelTva:
				return false;
			default:
				break;
			}
			const auto max = emu88Lib::mt32::maxValue(_param, _firmware);
			return emu88Lib::mt32::valueList(_param, _firmware).size() == max + 1u;
		}

		// The boards' pans run from the right: their sliders the other way round
		bool isLaPan(const emu88Lib::mt32::Param& _param)
		{
			return _param.text == emu88Lib::mt32::ParamText::PanLa || _param.text == emu88Lib::mt32::ParamText::PanPcm;
		}

		std::string laControlId(const emu88Lib::mt32::Area _area, const size_t _entry, const uint16_t _base,
			const size_t _param)
		{
			return "la" + std::to_string(static_cast<int>(_area)) + "_" + std::to_string(_entry) + "_" +
			       std::to_string(_base) + "_" + std::to_string(_param);
		}
	}

	std::string PartEditorWindow::laTimbreName(const uint8_t _group, const uint8_t _number) const
	{
		using namespace emu88Lib::mt32;
		// The timbre memory is the board's: known once written, or read by a data request
		if(_group == 2)
		{
			for(size_t i = 0; i < 10; ++i)
			{
				if(!m_laMemory.known(Area::TimbreMemory, _number, i))
					return {};
			}
			const auto* data = m_laMemory.entry(Area::TimbreMemory, _number);
			return data ? laName(data) : std::string();
		}
		return m_laPresets ? m_laPresets->timbre(_group, _number) : std::string();
	}

	std::string PartEditorWindow::laToneName(const uint8_t _media, const uint8_t _number) const
	{
		if(m_laPresets)
		{
			if(const auto& tone = m_laPresets->pcmTone(_media, _number); !tone.empty())
				return tone;
		}
		return (_media ? "Card tone " : "Tone ") + std::to_string(_number + 1);
	}

	std::string PartEditorWindow::laPartName(const LaPart& _part) const
	{
		using namespace emu88Lib::mt32;
		if(_part.board == Board::Pcm)
			return laToneName(m_laMemory.value(Area::PcmPatchTemp, _part.part, 0),
				m_laMemory.value(Area::PcmPatchTemp, _part.part, 1));
		if(_part.part == g_rhythmPart)
			return "Rhythm";

		// The timbre temp has the name of what the part plays, where a SysEx may have changed it. Otherwise the timbre
		// it was copied from: the timbre memory, or a preset in the ROM.
		bool known = true;
		for(size_t i = 0; i < 10; ++i)
			known &= m_laMemory.known(Area::TimbreTemp, _part.part, i);
		if(const auto* temp = m_laMemory.entry(Area::TimbreTemp, _part.part); known && temp)
		{
			if(auto name = laName(temp); !name.empty())
				return name;
		}
		const auto source = m_laMemory.timbreSource(_part.part);
		const auto name = laTimbreName(source.group, source.number);
		return name.empty() ? laTimbreCode(source.group, source.number) : name;
	}

	void PartEditorWindow::writeLa(const emu88Lib::mt32::Area _area, const size_t _entry, const uint16_t _offset,
		const std::vector<uint8_t>& _data)
	{
		// Through the copy first, which applies the board's rules: the partial reserves make room for each other, a
		// patch temp write takes the timbre again
		const auto address = emu88Lib::mt32::address({_area, static_cast<uint16_t>(_entry), _offset});
		const auto message = emu88Lib::mt32::dt1(address, _data);
		m_laMemory.processSysex(message.data(), message.size());
		setLaValue(address, _data);
	}

	PartEditorWindow::LaTimbre PartEditorWindow::laEditedTimbre() const
	{
		using namespace emu88Lib::mt32;
		const auto index = selectedPart();
		if(index >= m_laParts.size() || m_laParts[index].board != Board::La)
			return {};
		if(!isLaRhythm(index))
			return {Area::TimbreTemp, m_laParts[index].part, true};
		// A rhythm key plays its timbre straight from the timbre memory or the ROM: the memory's can be edited
		const auto timbre = m_laMemory.value(Area::RhythmSetup, m_laRhythmKey, 0);
		if(timbre < 64)
			return {Area::TimbreMemory, timbre, true};
		return {};
	}

	uint8_t PartEditorWindow::laDefault(const emu88Lib::mt32::Area _area, const size_t _entry,
		const uint16_t _offset) const
	{
		using namespace emu88Lib::mt32;
		// A timbre temp goes back to the timbre it was copied from
		if(_area == Area::TimbreTemp)
		{
			const auto source = m_laMemory.timbreSource(_entry);
			if(source.group == 2)
				return m_laMemory.value(Area::TimbreMemory, source.number, _offset);
			if(const auto* preset = m_laPresets ? m_laPresets->timbreData(source.group, source.number) : nullptr)
				return (*preset)[_offset];
			return m_laMemory.value(_area, _entry, _offset);
		}
		return m_laDefaults.value(_area, _entry, _offset);
	}

	bool PartEditorWindow::laPartialIsPcm(const LaTimbre& _timbre, const uint8_t _partial) const
	{
		using namespace emu88Lib::mt32;
		if(!_timbre.valid || _partial >= 4)
			return false;
		// Structure 1 & 2 at 0A, 3 & 4 at 0B
		const auto value = m_laMemory.value(_timbre.area, _timbre.entry, 0x0a + _partial / 2u);
		return structure(value).pcm[_partial % 2];
	}

	uint8_t PartEditorWindow::laStructureWith(const LaTimbre& _timbre, const uint8_t _partial, const bool _pcm) const
	{
		using namespace emu88Lib::mt32;
		const auto current = m_laMemory.value(_timbre.area, _timbre.entry, 0x0a + _partial / 2u);
		auto s = structure(current);
		s.pcm[_partial % 2] = _pcm;
		// A stereo pair is of one kind, and the plain mix has no synth first partial with a PCM second: the other
		// partial follows, as the board allows no more
		uint8_t value = current;
		if(structureValue(s.mix, s.pcm[0], s.pcm[1], value))
			return value;
		s.pcm[1 - _partial % 2] = _pcm;
		if(structureValue(s.mix, s.pcm[0], s.pcm[1], value))
			return value;
		return current;
	}

	std::string PartEditorWindow::laParamRow(const emu88Lib::mt32::Area _area, const size_t _entry,
		const emu88Lib::mt32::Block _block, const uint16_t _base, const char* _id, const LaInput _input,
		const char* _label, const bool _applies) const
	{
		using namespace emu88Lib::mt32;
		const auto index = findParamById(_block, _id);
		if(index < 0)
			return {};
		const auto& p = params(_block)[static_cast<size_t>(index)];
		const auto firmware = m_laMemory.firmware();
		const auto id = laControlId(_area, _entry, _base, static_cast<size_t>(index));
		const std::string label = _label ? _label : p.label;
		const auto input = _input != LaInput::Auto ? _input
		                 : isLaChoice(p, firmware) ? LaInput::Choice : LaInput::Slider;

		std::string rml;
		if(input == LaInput::Segments)
		{
			rml = "<div class=\"peRow\"><div id=\"peLbl" + id + "\" class=\"peRowLabel\" "
			      "title=\"Double-click for the default\">" + encode(label) + "</div><div id=\"peCtl" + id +
			      "\" class=\"peSegments\">";
			const auto values = valueList(p, firmware);
			for(size_t v = 0; v < values.size(); ++v)
			{
				rml += "<button id=\"peSeg" + id + "_" + std::to_string(v) + "\" class=\"peSegment\" value=\"" +
				       std::to_string(v) + "\">" + encode(values[v]) + "</button>";
			}
			rml += "</div></div>";
		}
		else
		{
			const auto note = p.text == ParamText::AssignMode
				? std::string("Single: a note played again ends the one sounding. Multi: they sound together. Last or "
				              "first: which notes keep their partials when they run out.")
				: std::string();
			rml = controlRow(id, label, input == LaInput::Choice, 0, maxValue(p, firmware), false, note);
		}
		// A setting that does not apply to what plays: greyed, not taken away
		if(!_applies)
			rml.replace(0, std::string("<div class=\"peRow").size(), "<div class=\"peRow na");
		return rml;
	}

	std::string PartEditorWindow::laNumberGrid(const emu88Lib::mt32::Area _area, const size_t _entry,
		const uint16_t _base, const char* _label, const std::vector<std::pair<const char*, const char*>>& _cells) const
	{
		using namespace emu88Lib::mt32;
		std::string rml = "<div class=\"peNumGrid\"><div class=\"peNumLabel\">" + encode(_label) + "</div>";
		for(const auto& [paramId, head] : _cells)
		{
			const auto index = findParamById(Block::Partial, paramId);
			if(index < 0)
				continue;
			const auto& p = params(Block::Partial)[static_cast<size_t>(index)];
			rml += "<div class=\"peNumCell\"><div class=\"peNumHead\">" + encode(head) + "</div><div id=\"peCtl" +
			       laControlId(_area, _entry, _base, static_cast<size_t>(index)) + "\" class=\"peNum\" title=\"" +
			       encode(std::string(p.section) + " " + p.label) +
			       ": drag or scroll, double-click for the default\"/></div>";
		}
		return rml + "</div>";
	}

	void PartEditorWindow::buildLaPartControls()
	{
		using namespace emu88Lib::mt32;
		m_laPartControls.clear();
		m_laKeySelect = nullptr;
		m_laKeyNames.clear();
		m_laNameInput = nullptr;
		m_laMuteButtons = {};
		m_laPlayChecks = {};
		m_laTabs = {};
		m_laTypeButtons = {};
		m_laShapeButtons = {};
		m_laBankButtons = {};
		m_laEnvelopeLines = {};
		m_laEnvelopeGrids = {};
		m_laNumberDrag.active = false;
		auto* container = m_root->GetElementById("pePartControls");
		const auto index = selectedPart();
		if(!container || index >= m_laParts.size())
			return;
		const auto& part = m_laParts[index];
		const bool old = m_laMemory.firmware() == LaFirmware::Old;

		const auto section = [](const std::string& _title, const std::string& _rows, const std::string& _note = {},
			const std::string& _class = {})
		{
			if(_rows.empty())
				return std::string();
			std::string rml = "<div class=\"peSection " + _class + "\"><div class=\"peSectionTitle\"";
			if(!_note.empty())
				rml += " title=\"" + encode(_note) + "\"";
			return rml + ">" + encode(_title) + "</div>" + _rows + "</div>";
		};
		const auto columns = [](const std::vector<std::string>& _columns, const char* _class)
		{
			std::string rml = std::string("<div class=\"peColumns ") + _class + "\">";
			for(const auto& c : _columns)
				rml += "<div class=\"peColumn\">" + c + "</div>";
			return rml + "</div>";
		};
		// A row of buttons that is not a parameter of its own
		const auto buttons = [](const char* _label, const char* _id, const std::vector<const char*>& _texts,
			const bool _applies, const char* _title = "")
		{
			std::string rml = std::string("<div class=\"peRow") + (_applies ? "" : " na") + "\" title=\"" + _title +
			                  "\"><div class=\"peRowLabel\">" + _label + "</div><div class=\"peSegments\">";
			for(size_t i = 0; i < _texts.size(); ++i)
				rml += "<button id=\"" + std::string(_id) + std::to_string(i) + "\" class=\"peSegment\">" + _texts[i] +
				       "</button>";
			return rml + "</div></div>";
		};

		if(part.board == Board::Pcm)
		{
			// The CM-32P's tones are its ROM's: the patch temp is all there is to edit, and fits
			const auto rows = [&](const char* _section, const std::vector<std::string>& _skip = {})
			{
				return section(_section, laControlRows(Area::PcmPatchTemp, part.part, Block::PcmPatchTemp, 0, _section,
					_skip));
			};
			container->SetInnerRML(columns({rows("Patch", {"ToneMedia", "ToneNumber"}),
				rows("Envelope") + rows("LFO") + rows("Part")}, ""));
			wireLaControls(container, Area::PcmPatchTemp, part.part, Block::PcmPatchTemp, 0, m_laPartControls);
			return;
		}

		const auto timbre = laEditedTimbre();
		if(!timbre.valid)
			m_laTab = 0;
		const auto pt = [&](const char* _id, const LaInput _input = LaInput::Auto)
		{
			return laParamRow(Area::PatchTemp, part.part, Block::PatchTemp, 0, _id, _input);
		};

		// Tabs: the common settings, then the four partials, each with its switch and its kind
		std::string rml;
		if(timbre.valid)
		{
			rml += "<div class=\"pePartialBar\"><button id=\"peLaTab0\" class=\"pePartial\">Common</button>";
			for(int p = 0; p < 4; ++p)
			{
				rml += "<button id=\"peLaTab" + std::to_string(p + 1) + "\" class=\"pePartial\"><div id=\"peLaPlay" +
				       std::to_string(p) + "\" class=\"peTabCheck\" title=\"Whether the partial plays\"/>Partial " +
				       std::to_string(p + 1) + "<span class=\"peKind\">" +
				       (laPartialIsPcm(timbre, static_cast<uint8_t>(p)) ? "PCM" : "Synth") + "</span></button>";
			}
			rml += "</div>";
		}

		if(m_laTab == 0)
		{
			// The part's settings, or the rhythm part's and its key's, beside the timbre's own
			std::string left;
			if(isLaRhythm(index))
			{
				// Its level; the rest of its patch temp it does not play. Each key has its settings, the list its
				// timbre.
				const std::string key = "<div class=\"peRow\"><div class=\"peRowLabel\">Key</div>"
				                        "<combo id=\"peLaKey\" class=\"peCombo\" popup=\"window\"><combotext/></combo>"
				                        "</div>";
				left = section("Part", pt("OutputLevel")) + section("Rhythm Key", key +
					laParamRow(Area::RhythmSetup, m_laRhythmKey, Block::RhythmKey, 0, "OutputLevel") +
					laParamRow(Area::RhythmSetup, m_laRhythmKey, Block::RhythmKey, 0, "Pan") +
					laParamRow(Area::RhythmSetup, m_laRhythmKey, Block::RhythmKey, 0, "ReverbSwitch"));
			}
			else
			{
				// The timbre is the list's. The MT-32 1.x takes it again at any write to the patch temp.
				left = section("Part", pt("KeyShift") + pt("FineTune") + pt("BenderRange") + pt("AssignMode") +
					pt("ReverbSwitch", LaInput::Segments) + pt("OutputLevel") + pt("Pan"), old ? "On the MT-32 1.x a "
					"change here loads the timbre again, which undoes changes to it." : "");
			}

			std::string right;
			if(!timbre.valid)
			{
				right = section("Timbre", "<div class=\"peNotice\">The key plays a rhythm timbre of the ROM, which "
					"cannot be changed. One of the memory timbres, i01 to i64, can.</div>");
			}
			else
			{
				std::string rows;
				if(!m_laMemory.entryKnown(timbre.area, timbre.entry))
				{
					rows += "<div class=\"peNotice\">The device has not told all of this timbre yet."
					        "<button id=\"peLaRead\" class=\"peNoticeButton\">Read it from the device</button></div>";
				}
				rows += "<div class=\"peRow\"><div class=\"peRowLabel\">Name</div>"
				        "<input id=\"peLaName\" class=\"peNameInput\" type=\"text\" maxlength=\"10\"/></div>";
				const auto common = [&](const char* _id, const LaInput _input, const char* _label)
				{
					return laParamRow(timbre.area, timbre.entry, Block::TimbreCommon, 0, _id, _input, _label);
				};
				rows += common("Structure12", LaInput::Choice, "Partials 1 & 2") +
				        common("Structure34", LaInput::Choice, "Partials 3 & 4") +
				        common("EnvMode", LaInput::Segments, "Envelopes");
				rows += "<div class=\"peRow\"><div class=\"peRowLabel\" title=\"The partials that play\">Partials</div>"
				        "<div class=\"peMutes\">";
				for(int p = 0; p < 4; ++p)
				{
					rows += "<button id=\"peLaMute" + std::to_string(p) + "\" class=\"peMute\" title=\"" +
					        (laPartialIsPcm(timbre, static_cast<uint8_t>(p)) ? "PCM sample" : "Synth wave") + "\">" +
					        std::to_string(p + 1) + (laPartialIsPcm(timbre, static_cast<uint8_t>(p)) ? " P" : " S") +
					        "</button>";
				}
				rows += "</div></div><div class=\"peHint\">S a synth wave, P a PCM sample, R the ring modulator: "
				        "it multiplies the two partials of a pair.</div>";
				right = section(timbre.area == Area::TimbreMemory
					? "Memory Timbre " + laTimbreCode(2, static_cast<uint8_t>(timbre.entry)) : std::string("Timbre"),
					rows, timbre.area == Area::TimbreMemory ? "A timbre of the memory: every key and part that plays "
					"it plays it as changed here" : "");
			}
			rml += columns({left, right}, "");
		}
		else
		{
			// One partial: what it plays at the top, then its three envelopes with their graphs, side by side
			const auto partial = static_cast<uint8_t>(m_laTab - 1);
			const auto base = laPartialBase(partial);
			const bool pcm = laPartialIsPcm(timbre, partial);
			const auto row = [&](const char* _id, const LaInput _input = LaInput::Auto, const char* _label = nullptr,
				const bool _applies = true)
			{
				return laParamRow(timbre.area, timbre.entry, Block::Partial, base, _id, _input, _label, _applies);
			};
			const auto grid = [&](const char* _label, const std::vector<std::pair<const char*, const char*>>& _cells)
			{
				return laNumberGrid(timbre.area, timbre.entry, base, _label, _cells);
			};
			const auto graph = [](const int _i)
			{
				const auto n = std::to_string(_i);
				return "<div class=\"peEnvGraph\"><strokes id=\"peEnvGrid" + n + "\" class=\"peEnvGrid\"/>"
				       "<strokes id=\"peEnvLine" + n + "\" class=\"peEnvLine\"/></div>";
			};

			// The waveform byte holds the synth wave's shape in bit 0 and the PCM bank in bit 1; the CM-32L has the
			// second bank, the MT-32 1.x none
			const auto wave = buttons("Kind", "peLaType", {"Synth", "PCM"}, true,
				"Sets the structure of the pair: the other partial may follow, see Common") +
				(pcm ? row("PcmWave", LaInput::Choice) : buttons("Waveform", "peLaShape", {"Square", "Saw"}, true)) +
				buttons("PCM Bank", "peLaBank", {"1", "2"}, pcm && !old, "The CM-32L's second bank of samples") +
				row("PulseWidth", LaInput::Slider, nullptr, !pcm) + row("PwVeloSens", LaInput::Slider, nullptr, !pcm);
			const auto pitch = row("PitchCoarse", LaInput::Slider, "Coarse") +
				row("PitchFine", LaInput::Slider, "Fine") + row("PitchKeyfollow", LaInput::Choice, "Keyfollow") +
				row("BenderSwitch", LaInput::Segments, "Bender");
			const auto lfo = row("LfoRate") + row("LfoDepth") + row("LfoModSens");
			rml += columns({section("Wave", wave), section("Pitch", pitch), section("Pitch LFO", lfo)}, "three");

			const auto penv = graph(0) +
				grid("Time", {{"PenvTime1", "1"}, {"PenvTime2", "2"}, {"PenvTime3", "3"}, {"PenvTime4", "4"}}) +
				grid("Level", {{"PenvLevel0", "0"}, {"PenvLevel1", "1"}, {"PenvLevel2", "2"}, {"PenvSustain", "S"},
					{"PenvEnd", "E"}}) +
				row("PenvDepth") + row("PenvVeloSens") + row("PenvTimeKeyf");
			const auto tvf = graph(1) +
				grid("Time", {{"TvfTime1", "1"}, {"TvfTime2", "2"}, {"TvfTime3", "3"}, {"TvfTime4", "4"},
					{"TvfTime5", "5"}}) +
				grid("Level", {{"TvfLevel1", "1"}, {"TvfLevel2", "2"}, {"TvfLevel3", "3"}, {"TvfSustain", "S"}}) +
				row("TvfCutoff") + row("TvfResonance") + row("TvfKeyfollow", LaInput::Choice) +
				row("TvfBiasPoint", LaInput::Slider) + row("TvfBiasLevel") + row("TvfEnvDepth") +
				row("TvfEnvVeloSens") + row("TvfEnvDepthKeyf") + row("TvfEnvTimeKeyf");
			const auto tva = graph(2) +
				grid("Time", {{"TvaTime1", "1"}, {"TvaTime2", "2"}, {"TvaTime3", "3"}, {"TvaTime4", "4"},
					{"TvaTime5", "5"}}) +
				grid("Level", {{"TvaLevel1", "1"}, {"TvaLevel2", "2"}, {"TvaLevel3", "3"}, {"TvaSustain", "S"}}) +
				row("TvaLevel") + row("TvaVeloSens") + row("TvaBiasPoint1", LaInput::Slider) + row("TvaBiasLevel1") +
				row("TvaBiasPoint2", LaInput::Slider) + row("TvaBiasLevel2") + row("TvaEnvTimeKeyf") +
				row("TvaEnvTimeVelo");
			// A PCM partial has no filter
			rml += columns({section("Pitch Envelope", penv),
				section("Filter", tvf, pcm ? "A PCM partial has no filter" : "", pcm ? "na" : ""),
				section("Amplifier", tva)}, "three");
		}

		const auto mute = timbre.valid ? m_laMemory.value(timbre.area, timbre.entry, 0x0c) : 0;
		const bool off = m_laTab > 0 && !(mute >> (m_laTab - 1) & 1);
		container->SetInnerRML("<div class=\"peLaPage" + std::string(off ? " off" : "") + "\">" + rml + "</div>");

		wireLaControls(container, Area::PatchTemp, part.part, Block::PatchTemp, 0, m_laPartControls);
		if(isLaRhythm(index))
		{
			wireLaControls(container, Area::RhythmSetup, m_laRhythmKey, Block::RhythmKey, 0, m_laPartControls);
			m_laKeySelect = dynamic_cast<juceRmlUi::ElemComboBox*>(container->GetElementById("peLaKey"));
			if(m_laKeySelect)
			{
				juceRmlUi::EventListener::Add(m_laKeySelect, Rml::EventId::Change, [this](Rml::Event&)
				{
					const auto key = m_laKeySelect->getSelectedIndex();
					if(m_updatingUi || key < 0)
						return;
					m_laRhythmKey = static_cast<size_t>(key);
					m_listKey.clear();
					refreshPart();
				});
			}
		}
		if(!timbre.valid)
			return;

		wireLaControls(container, timbre.area, timbre.entry, Block::TimbreCommon, 0, m_laPartControls);
		if(m_laTab > 0)
		{
			wireLaControls(container, timbre.area, timbre.entry, Block::Partial,
				laPartialBase(static_cast<uint8_t>(m_laTab - 1)), m_laPartControls);
		}

		addClick(container->GetElementById("peLaRead"), [this, timbre]
		{
			laReceive({{emu88Lib::mt32::address({timbre.area, static_cast<uint16_t>(timbre.entry), 0}),
				static_cast<uint32_t>(g_timbreSize)}});
		});

		// The name, written as the board keeps it: ten characters, filled with spaces
		m_laNameInput = dynamic_cast<Rml::ElementFormControlInput*>(container->GetElementById("peLaName"));
		if(m_laNameInput)
		{
			juceRmlUi::EventListener::Add(m_laNameInput, Rml::EventId::Change, [this, timbre](Rml::Event&)
			{
				if(m_updatingUi)
					return;
				auto name = m_laNameInput->GetValue();
				name.resize(10, ' ');
				std::vector<uint8_t> data;
				for(const auto c : name)
					data.push_back(static_cast<uint8_t>(c >= 0x20 && c < 0x7f ? c : ' '));
				writeLa(timbre.area, timbre.entry, 0, data);
			});
		}

		// The partials' switches, here and in the tabs
		const auto muteParam = findParamById(Block::TimbreCommon, "PartialMute");
		const auto toggle = [this, timbre, muteParam](const uint8_t _p)
		{
			if(muteParam < 0)
				return;
			const auto mute = m_laMemory.value(timbre.area, timbre.entry, 0x0c);
			setLaParam(timbre.area, timbre.entry, Block::TimbreCommon, 0, static_cast<size_t>(muteParam),
				static_cast<uint8_t>(mute ^ (1u << _p)));
			m_laControlsKey.clear();
			refreshPart();
		};
		for(uint8_t p = 0; p < 4; ++p)
		{
			m_laMuteButtons[p] = container->GetElementById("peLaMute" + std::to_string(p));
			addClick(m_laMuteButtons[p], [toggle, p] { toggle(p); });
			m_laPlayChecks[p] = container->GetElementById("peLaPlay" + std::to_string(p));
			if(m_laPlayChecks[p])
			{
				juceRmlUi::EventListener::Add(m_laPlayChecks[p], Rml::EventId::Click, [toggle, p](Rml::Event& _event)
				{
					_event.StopPropagation();
					toggle(p);
				});
			}
		}
		for(uint8_t t = 0; t < 5; ++t)
		{
			m_laTabs[t] = container->GetElementById("peLaTab" + std::to_string(t));
			addClick(m_laTabs[t], [this, t]
			{
				m_laTab = t;
				m_laControlsKey.clear();
				refreshPart();
			});
		}

		if(m_laTab == 0)
			return;
		const auto partial = static_cast<uint8_t>(m_laTab - 1);
		const auto waveParam = findParamById(Block::Partial, "Waveform");
		const auto waveOffset = static_cast<uint16_t>(laPartialBase(partial) + 0x04);
		// The kind sets the pair's structure; the shape and the bank share the waveform byte
		const auto setWave = [this, timbre, partial, waveParam, waveOffset](const uint8_t _mask, const uint8_t _bits)
		{
			if(waveParam < 0)
				return;
			const auto current = m_laMemory.value(timbre.area, timbre.entry, waveOffset);
			setLaParam(timbre.area, timbre.entry, Block::Partial, laPartialBase(partial),
				static_cast<size_t>(waveParam), static_cast<uint8_t>((current & ~_mask) | _bits));
		};
		for(uint8_t i = 0; i < 2; ++i)
		{
			m_laTypeButtons[i] = container->GetElementById("peLaType" + std::to_string(i));
			addClick(m_laTypeButtons[i], [this, timbre, partial, i]
			{
				const auto structureParam = findParamById(Block::TimbreCommon,
					partial < 2 ? "Structure12" : "Structure34");
				if(structureParam < 0)
					return;
				setLaParam(timbre.area, timbre.entry, Block::TimbreCommon, 0, static_cast<size_t>(structureParam),
					laStructureWith(timbre, partial, i == 1));
				m_laControlsKey.clear();
				refreshPart();
			});
			m_laShapeButtons[i] = container->GetElementById("peLaShape" + std::to_string(i));
			addClick(m_laShapeButtons[i], [setWave, i] { setWave(0x01, i); });
			m_laBankButtons[i] = container->GetElementById("peLaBank" + std::to_string(i));
			addClick(m_laBankButtons[i], [setWave, i] { setWave(0x02, static_cast<uint8_t>(i << 1)); });
		}
		for(int e = 0; e < 3; ++e)
		{
			m_laEnvelopeLines[e] = container->GetElementById("peEnvLine" + std::to_string(e));
			m_laEnvelopeGrids[e] = container->GetElementById("peEnvGrid" + std::to_string(e));
		}
	}

	void PartEditorWindow::dragLaNumber(const float _y)
	{
		if(!m_laNumberDrag.active)
			return;
		// Three dp a step, up to more
		const auto& c = m_laNumberDrag.control;
		const auto ratio = m_root->GetContext() ? m_root->GetContext()->GetDensityIndependentPixelRatio() : 1.0f;
		const auto steps = static_cast<int>((m_laNumberDrag.startY - _y) / (3.0f * ratio));
		const auto value = std::clamp(m_laNumberDrag.startValue + steps, 0, m_laNumberDrag.max);
		setLaParam(c.area, c.entry, c.block, c.base, c.param, static_cast<uint8_t>(value));
	}

	void PartEditorWindow::refreshLaTimbreEditor()
	{
		const auto timbre = laEditedTimbre();
		if(!timbre.valid)
			return;
		m_updatingUi = true;
		// Not under the cursor of someone typing
		if(m_laNameInput && !m_laNameInput->IsPseudoClassSet("focus"))
		{
			const auto* data = m_laMemory.entry(timbre.area, timbre.entry);
			const auto name = data ? laName(data) : std::string();
			if(m_laNameInput->GetValue() != name)
				m_laNameInput->SetValue(name);
		}
		const auto mute = m_laMemory.value(timbre.area, timbre.entry, 0x0c);
		for(size_t p = 0; p < 4; ++p)
		{
			const bool plays = (mute >> p & 1) != 0;
			if(m_laMuteButtons[p])
				m_laMuteButtons[p]->SetPseudoClass("checked", plays);
			if(m_laPlayChecks[p])
				m_laPlayChecks[p]->SetPseudoClass("checked", plays);
			if(m_laTabs[p + 1])
				m_laTabs[p + 1]->SetClass("muted", !plays);
		}
		for(size_t t = 0; t < m_laTabs.size(); ++t)
		{
			if(m_laTabs[t])
				m_laTabs[t]->SetPseudoClass("checked", t == m_laTab);
		}
		if(m_laTab > 0)
		{
			const auto partial = static_cast<uint8_t>(m_laTab - 1);
			const bool pcm = laPartialIsPcm(timbre, partial);
			const auto wave = m_laMemory.value(timbre.area, timbre.entry, laPartialBase(partial) + 0x04u);
			for(uint8_t i = 0; i < 2; ++i)
			{
				if(m_laTypeButtons[i])
					m_laTypeButtons[i]->SetPseudoClass("checked", pcm == (i == 1));
				if(m_laShapeButtons[i])
					m_laShapeButtons[i]->SetPseudoClass("checked", (wave & 1) == i);
				if(m_laBankButtons[i])
					m_laBankButtons[i]->SetPseudoClass("checked", (wave >> 1 & 1) == i);
			}
		}
		m_updatingUi = false;
		refreshLaEnvelopes();
	}

	void PartEditorWindow::refreshLaEnvelopes()
	{
		using Points = juceRmlUi::ElemStrokes::Points;
		const auto timbre = laEditedTimbre();
		if(!timbre.valid || m_laTab == 0)
			return;
		const auto base = laPartialBase(static_cast<uint8_t>(m_laTab - 1));
		const auto at = [&](const size_t _offset)
		{
			return static_cast<float>(m_laMemory.value(timbre.area, timbre.entry, base + _offset));
		};

		// Fixed in the style sheet, in dp
		constexpr float w = 216.0f, h = 60.0f, pad = 3.0f;
		// The times as they are, a time of 0 straight up or down, on a scale that fits them, but no wider than one of
		// a hundred to the whole: short envelopes look short. The sustain takes a fifth of the width.
		const auto draw = [&](const int _graph, const float _start, const std::vector<std::pair<float, float>>& _attack,
			const std::pair<float, float>& _release, const bool _signed)
		{
			const auto y = [&](const float _level)
			{
				return _signed ? h * 0.5f - (_level - 50.0f) / 50.0f * (h * 0.5f - pad)
				               : h - pad - _level / 100.0f * (h - 2 * pad);
			};
			float total = _release.first;
			for(const auto& [time, level] : _attack)
				total += time;
			const auto unit = (w - 2 * pad) * 0.8f / std::max(total, 100.0f);
			Points line{{pad, y(_start)}};
			// A point where the last one is adds nothing
			const auto add = [&](const float _x, const float _y)
			{
				if(line.back().x != _x || line.back().y != _y)
					line.push_back({_x, _y});
			};
			float x = pad;
			for(const auto& [time, level] : _attack)
			{
				x += time * unit;
				add(x, y(level));
			}
			// The sustain, until the key is let go
			x += (w - 2 * pad) * 0.2f;
			const auto keyOff = x;
			add(x, line.back().y);
			x += _release.first * unit;
			add(x, y(_release.second));
			// What is left of the width after it: nothing
			add(w - pad, line.back().y);

			if(auto* strokes = dynamic_cast<juceRmlUi::ElemStrokes*>(m_laEnvelopeLines[_graph]))
				strokes->setStrokes({line});
			if(auto* strokes = dynamic_cast<juceRmlUi::ElemStrokes*>(m_laEnvelopeGrids[_graph]))
				strokes->setStrokes({{{pad, y(_signed ? 50.0f : 0.0f)}, {w - pad, y(_signed ? 50.0f : 0.0f)}},
					{{keyOff, pad}, {keyOff, h - pad}}});
		};

		// The pitch envelope from level 0 through 1 and 2 to the sustain, and after the key to the end level, all
		// around the pitch played. The filter and the amplifier from nothing through three levels to the sustain, and
		// back to nothing.
		draw(0, at(0x0f), {{at(0x0b), at(0x10)}, {at(0x0c), at(0x11)}, {at(0x0d), at(0x12)}}, {at(0x0e), at(0x13)},
			true);
		draw(1, 0.0f, {{at(0x20), at(0x25)}, {at(0x21), at(0x26)}, {at(0x22), at(0x27)}, {at(0x23), at(0x28)}},
			{at(0x24), 0.0f}, false);
		draw(2, 0.0f, {{at(0x31), at(0x36)}, {at(0x32), at(0x37)}, {at(0x33), at(0x38)}, {at(0x34), at(0x39)}},
			{at(0x35), 0.0f}, false);
	}

	std::vector<PartEditorWindow::LaListRow> PartEditorWindow::laListRows() const
	{
		using namespace emu88Lib::mt32;
		std::vector<LaListRow> rows;
		const auto index = selectedPart();
		if(index >= m_laParts.size())
			return rows;
		const auto& part = m_laParts[index];

		const auto add = [&](const uint8_t _group, const uint8_t _number, const std::string& _label,
			const std::string& _name)
		{
			if(matchesFilter(_name) || matchesFilter(_label))
				rows.push_back({false, _group, _number, _label, _name});
		};
		const auto heading = [&](const std::string& _text)
		{
			// One that the filter left nothing under goes
			if(!rows.empty() && rows.back().heading)
				rows.pop_back();
			rows.push_back({true, 0, 0, {}, _text});
		};

		if(isLaRhythm(index))
		{
			// A key plays one of the rhythm timbres in the ROM, values 64-127, or one of the timbre memory, 0-63
			heading("Rhythm timbres");
			for(uint8_t n = 0; n < 64; ++n)
			{
				if(const auto name = laTimbreName(3, n); !name.empty())
					add(0, static_cast<uint8_t>(64 + n), laTimbreCode(3, n), name);
			}
			heading("Memory timbres");
			for(uint8_t n = 0; n < 64; ++n)
			{
				const auto name = laTimbreName(2, n);
				add(0, n, laTimbreCode(2, n), name.empty() ? "-" : name);
			}
		}
		else if(part.board == Board::La)
		{
			static constexpr const char* g_groups[] = {"Group a", "Group b", "Group i, memory", "Group r, rhythm"};
			for(uint8_t g = 0; g < 4; ++g)
			{
				heading(g_groups[g]);
				for(uint8_t n = 0; n < 64; ++n)
				{
					const auto name = laTimbreName(g, n);
					// The ROM's groups end where their timbres do; the memory has all 64
					if(name.empty() && g != 2)
						continue;
					add(g, n, laTimbreCode(g, n), name.empty() ? "-" : name);
				}
			}
		}
		else
		{
			heading("Internal");
			for(uint8_t n = 0; n < g_pcmInternalTones; ++n)
				add(0, n, number(n + 1), laToneName(0, n));
			// The card's, when one is in
			heading("Card");
			for(uint8_t n = 0; n < 128; ++n)
			{
				if(m_laPresets && !m_laPresets->pcmTone(1, n).empty())
					add(1, n, number(n + 1), m_laPresets->pcmTone(1, n));
			}
		}
		if(!rows.empty() && rows.back().heading)
			rows.pop_back();
		return rows;
	}

	std::pair<uint8_t, uint8_t> PartEditorWindow::laListSelection() const
	{
		using namespace emu88Lib::mt32;
		const auto index = selectedPart();
		if(index >= m_laParts.size())
			return {0xff, 0xff};
		const auto& part = m_laParts[index];
		if(isLaRhythm(index))
			return {0, m_laMemory.value(Area::RhythmSetup, m_laRhythmKey, 0)};
		if(part.board == Board::La)
		{
			const auto source = m_laMemory.timbreSource(part.part);
			return {source.group, source.number};
		}
		return {m_laMemory.value(Area::PcmPatchTemp, part.part, 0), m_laMemory.value(Area::PcmPatchTemp, part.part, 1)};
	}

	void PartEditorWindow::selectLaListRow(const LaListRow& _row)
	{
		using namespace emu88Lib::mt32;
		const auto index = selectedPart();
		if(_row.heading || index >= m_laParts.size())
			return;
		const auto& part = m_laParts[index];

		if(isLaRhythm(index))
			writeLa(Area::RhythmSetup, m_laRhythmKey, 0, {_row.number});
		else
			// The timbre group and number, or the tone media and number, which the board takes the timbre or tone by
			writeLa(part.board == Board::La ? Area::PatchTemp : Area::PcmPatchTemp, part.part, 0,
				{_row.group, _row.number});
		flushPending();
		refresh();
	}

	void PartEditorWindow::applyLaListSelection()
	{
		const auto [group, number] = laListSelection();
		const auto selection = std::to_string(group) + '/' + std::to_string(number);
		if(selection == m_listSelection)
			return;
		m_listSelection = selection;

		Rml::Element* selected = nullptr;
		for(size_t r = 0; r < m_laListRows.size() && r < m_laListElements.size(); ++r)
		{
			auto* element = m_laListElements[r];
			if(!element)
				continue;
			const auto& row = m_laListRows[r];
			const bool isSelected = row.group == group && row.number == number;
			element->SetClass("selected", isSelected);
			if(isSelected)
				selected = element;
		}
		if(selected)
		{
			// A list built this very moment has no layout yet to scroll by
			selected->GetOwnerDocument()->UpdateDocument();
			selected->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest,
				Rml::ScrollAlignment::Nearest, Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest));
		}
	}

	std::string PartEditorWindow::laPartTitle(const size_t _row) const
	{
		if(_row >= m_laParts.size())
			return "Part";
		if(isLaRhythm(_row))
			return "Rhythm Part";
		const auto& part = m_laParts[_row];
		const bool both = m_laMemory.hasLa() && m_laMemory.hasPcm();
		const std::string board = !both ? "" : part.board == emu88Lib::mt32::Board::La ? "LA " : "PCM ";
		return board + "Part " + std::to_string(part.part + 1);
	}

	std::vector<uint8_t> PartEditorWindow::laRhythmMemoryTimbres() const
	{
		using namespace emu88Lib::mt32;
		std::vector<uint8_t> timbres;
		for(size_t k = 0; k < m_laMemory.entryCount(Area::RhythmSetup); ++k)
		{
			const auto timbre = m_laMemory.value(Area::RhythmSetup, k, 0);
			if(timbre < 64 && std::find(timbres.begin(), timbres.end(), timbre) == timbres.end())
				timbres.push_back(timbre);
		}
		std::sort(timbres.begin(), timbres.end());
		return timbres;
	}

	std::vector<emu88Lib::mt32::Message> PartEditorWindow::laPartMessages(const size_t _row) const
	{
		using namespace emu88Lib::mt32;
		std::vector<Message> messages;
		if(_row >= m_laParts.size())
			return messages;
		const auto& part = m_laParts[_row];
		const auto entryBytes = [&](const Area _area, const size_t _entry, const size_t _first, const size_t _count)
		{
			std::vector<uint8_t> data;
			for(size_t i = _first; i < _first + _count; ++i)
				data.push_back(m_laMemory.value(_area, _entry, i));
			return data;
		};
		const auto write = [&](const Area _area, const size_t _entry, const size_t _first,
			const std::vector<uint8_t>& _data)
		{
			const auto address = emu88Lib::mt32::address({_area, static_cast<uint16_t>(_entry),
				static_cast<uint16_t>(_first)});
			for(auto& m : dt1Chunks(address, _data.data(), _data.size()))
				messages.push_back(std::move(m));
		};

		if(part.board == Board::Pcm)
		{
			write(Area::PcmPatchTemp, part.part, 0, entryBytes(Area::PcmPatchTemp, part.part, 0, g_pcmPatchTempSize));
		}
		else if(isLaRhythm(_row))
		{
			// Its level, every key, and the memory timbres they play, as far as they are known
			write(Area::PatchTemp, part.part, 8, {m_laMemory.value(Area::PatchTemp, part.part, 8)});
			const auto keys = m_laMemory.entryCount(Area::RhythmSetup);
			std::vector<uint8_t> data;
			for(size_t k = 0; k < keys; ++k)
			{
				for(size_t i = 0; i < g_rhythmKeySize; ++i)
					data.push_back(m_laMemory.value(Area::RhythmSetup, k, i));
			}
			write(Area::RhythmSetup, 0, 0, data);
			for(const auto timbre : laRhythmMemoryTimbres())
			{
				if(m_laMemory.entryKnown(Area::TimbreMemory, timbre))
					write(Area::TimbreMemory, timbre, 0, entryBytes(Area::TimbreMemory, timbre, 0, g_timbreSize));
			}
		}
		else
		{
			// The patch temp, which takes the timbre again, then the timbre as the part has it, which a SysEx may
			// have changed: when it is known
			write(Area::PatchTemp, part.part, 0, entryBytes(Area::PatchTemp, part.part, 0, 10));
			if(m_laMemory.entryKnown(Area::TimbreTemp, part.part))
				write(Area::TimbreTemp, part.part, 0, entryBytes(Area::TimbreTemp, part.part, 0, g_timbreSize));
		}
		return messages;
	}

	std::vector<emu88Lib::mt32::Message> PartEditorWindow::laSystemMessages() const
	{
		using namespace emu88Lib::mt32;
		std::vector<Message> messages;
		using System = std::tuple<Area, size_t, bool>;
		for(const auto& [area, size, present] : {System{Area::LaSystem, g_laSystemSize, m_laMemory.hasLa()},
		                                         System{Area::PcmSystem, g_pcmSystemSize, m_laMemory.hasPcm()}})
		{
			if(!present)
				continue;
			std::vector<uint8_t> data;
			for(size_t i = 0; i < size; ++i)
				data.push_back(m_laMemory.value(area, 0, i));
			messages.push_back(dt1(emu88Lib::mt32::address({area, 0, 0}), data));
		}
		return messages;
	}

	uint32_t PartEditorWindow::laPause(const emu88Lib::mt32::Message& _message, bool& _pcmPatchMemorySeen) const
	{
		using namespace emu88Lib::mt32;
		constexpr uint32_t restorePause = g_restoreResetDelayMs * 1000 * 5 / 2;
		// The time on the wire and room for the firmware to take the message, 40 ms and a millisecond a byte, and
		// four times that for the MT-32 1.x, which loses SysEx that come closer; mt32Firmware_test paces its traffic
		// the same
		const auto old = m_laMemory.firmware() == LaFirmware::Old;
		const auto pause = static_cast<uint32_t>((40000 + _message.size() * 1344) * (old ? 4 : 1));
		Dt1 dt1;
		if(!parseDt1(_message.data(), _message.size(), dt1))
			return pause;
		if((dt1.address >> 16) == (g_allParametersReset >> 16))
			return std::max(pause, restorePause);
		if(!_pcmPatchMemorySeen && locate(Board::Pcm, dt1.address, LaFirmware::New).area == Area::PcmPatchMemory)
		{
			_pcmPatchMemorySeen = true;
			return std::max(pause, restorePause);
		}
		return pause;
	}

	uint32_t PartEditorWindow::laPauseTicks(const emu88Lib::mt32::Message& _message, bool& _pcmPatchMemorySeen) const
	{
		return (laPause(_message, _pcmPatchMemorySeen) + g_tickMicroseconds - 1) / g_tickMicroseconds;
	}

	void PartEditorWindow::laSend(std::vector<emu88Lib::mt32::Message> _messages, const std::string& _done)
	{
		if(laSending() || _messages.empty())
			return;
		flushPending();
		bool pcmPatchMemory = false;
		for(auto& message : _messages)
		{
			const auto ticks = laPauseTicks(message, pcmPatchMemory);
			m_laSendQueue.push_back({std::move(message), ticks});
		}
		m_laSendTotal = m_laSendQueue.size();
		m_laSendWait = 0;
		m_laSendDone = _done;
		showStatus("Sending...");
		refreshActions();
	}

	std::vector<std::pair<uint32_t, uint32_t>> PartEditorWindow::laPartRanges(const size_t _row) const
	{
		using namespace emu88Lib::mt32;
		std::vector<std::pair<uint32_t, uint32_t>> ranges;
		if(_row >= m_laParts.size() || m_laParts[_row].board != Board::La)
			return ranges;
		const auto part = m_laParts[_row].part;
		const auto add = [&](const Area _area, const size_t _entry, const size_t _size)
		{
			ranges.emplace_back(emu88Lib::mt32::address({_area, static_cast<uint16_t>(_entry), 0}),
				static_cast<uint32_t>(_size));
		};
		add(Area::PatchTemp, part, g_patchTempSize);
		if(isLaRhythm(_row))
		{
			add(Area::RhythmSetup, 0, m_laMemory.entryCount(Area::RhythmSetup) * g_rhythmKeySize);
			for(const auto timbre : laRhythmMemoryTimbres())
				add(Area::TimbreMemory, timbre, g_timbreSize);
		}
		else
		{
			add(Area::TimbreTemp, part, g_timbreSize);
		}
		return ranges;
	}

	std::vector<std::pair<uint32_t, uint32_t>> PartEditorWindow::laAllRanges() const
	{
		using namespace emu88Lib::mt32;
		std::vector<std::pair<uint32_t, uint32_t>> ranges;
		if(!m_laMemory.hasLa())
			return ranges;
		// Every area of the LA board. A timbre is one request each: the timbre memory spaces them 256 apart, and the
		// board takes its time over a long answer, which one request per timbre keeps within its timeout.
		for(const auto area : {Area::LaSystem, Area::PatchTemp, Area::RhythmSetup, Area::TimbreTemp, Area::PatchMemory,
		                       Area::TimbreMemory})
		{
			const auto& info = areaInfo(area);
			const auto count = m_laMemory.entryCount(area);
			if(area != Area::TimbreTemp && info.entryStride == info.entrySize)
			{
				ranges.emplace_back(info.address, static_cast<uint32_t>(count * info.entrySize));
				continue;
			}
			for(uint16_t e = 0; e < count; ++e)
				ranges.emplace_back(emu88Lib::mt32::address({area, e, 0}), static_cast<uint32_t>(info.entrySize));
		}
		return ranges;
	}

	void PartEditorWindow::laReceive(const std::vector<std::pair<uint32_t, uint32_t>>& _ranges)
	{
		flushPending();
		std::vector<Processor::DataRequest> requests;
		for(const auto& [address, size] : _ranges)
		{
			Processor::DataRequest request;
			request.sysex = emu88Lib::mt32::rq1(address, size);
			request.address = address;
			request.size = size;
			// The board answers in pieces of 256 bytes, each a DT1 on the wire at 320 us a byte
			request.timeoutSeconds = 0.15 + size * 320e-6 * 1.5;
			requests.push_back(std::move(request));
		}
		if(requests.empty() || !m_processor.startDataRequests(std::move(requests)))
		{
			showStatus("The device cannot take data requests right now.", true);
			return;
		}
		m_receiving = true;
		showStatus("Receiving...");
		refreshActions();
	}

	void PartEditorWindow::startAudition(const uint8_t _port, const uint8_t _channel, const uint8_t _note)
	{
		stopAudition();
		if(!m_audition || _channel >= 16)
			return;
		// After what picked the sound
		flushPending();
		synthLib::SMidiEvent on(synthLib::MidiEventSource::Host, static_cast<uint8_t>(0x90 | _channel), _note, 100);
		on.port = _port;
		m_processor.sendMidiEvents({on});
		m_auditionNote = {true, _port, _channel, _note};
	}

	void PartEditorWindow::stopAudition()
	{
		if(!m_auditionNote.active)
			return;
		synthLib::SMidiEvent off(synthLib::MidiEventSource::Host, static_cast<uint8_t>(0x80 | m_auditionNote.channel),
			m_auditionNote.note, 0);
		off.port = m_auditionNote.port;
		m_processor.sendMidiEvents({off});
		m_auditionNote.active = false;
	}

	void PartEditorWindow::auditionPart(const size_t _part)
	{
		using namespace emu88Lib::mt32;
		if(m_laDialect)
		{
			if(_part >= m_laParts.size())
				return;
			const auto& part = m_laParts[_part];
			if(part.board == Board::Pcm)
				startAudition(0, m_laMemory.pcmChannel(part.part), 60);
			else
				startAudition(0, m_laMemory.laReceiveChannel(part.part),
					isLaRhythm(_part) ? static_cast<uint8_t>(g_firstRhythmKey + m_laRhythmKey) : 60);
			return;
		}
		if(_part >= m_setup.parts.size())
			return;
		const auto& p = m_setup.parts[_part];
		startAudition(static_cast<uint8_t>(_part / gs::g_partsPerGroup), p.rxChannel, p.isDrumPart() ? 38 : 60);
	}

	void PartEditorWindow::moveListSelection(const int _delta)
	{
		if(m_page == g_mixerPage)
			return;
		const auto step = [&](const int _current, const int _count, const auto& _skip)
		{
			// From the row played, or from an end
			auto next = _current < 0 ? (_delta > 0 ? -1 : _count) : _current;
			do
				next += _delta;
			while(next >= 0 && next < _count && _skip(next));
			return next >= 0 && next < _count ? next : -1;
		};
		const auto index = selectedPart();

		if(m_laDialect)
		{
			const auto [group, number] = laListSelection();
			int current = -1;
			for(size_t r = 0; r < m_laListRows.size() && current < 0; ++r)
			{
				const auto& row = m_laListRows[r];
				if(!row.heading && row.group == group && row.number == number)
					current = static_cast<int>(r);
			}
			const auto next = step(current, static_cast<int>(m_laListRows.size()),
				[&](const int _r) { return m_laListRows[static_cast<size_t>(_r)].heading; });
			if(next < 0)
				return;
			selectLaListRow(m_laListRows[static_cast<size_t>(next)]);
			auditionPart(index);
			return;
		}

		if(index >= m_setup.parts.size())
			return;
		const auto& p = m_setup.parts[index];
		int current = -1;
		for(size_t r = 0; r < m_listRows.size() && current < 0; ++r)
		{
			const auto& row = m_listRows[r];
			if(row.program == p.program && (p.isDrumPart() || row.bankMsb == p.bankMsb))
				current = static_cast<int>(r);
		}
		const auto next = step(current, static_cast<int>(m_listRows.size()), [](int) { return false; });
		if(next < 0)
			return;
		const auto& row = m_listRows[static_cast<size_t>(next)];
		selectTone(index, p.isDrumPart() ? 0 : row.bankMsb, row.program);
		auditionPart(index);
	}

	void PartEditorWindow::refreshLaPart()
	{
		using namespace emu88Lib::mt32;
		const auto index = selectedPart();
		if(index >= m_laParts.size())
			return;
		const auto& part = m_laParts[index];
		const bool la = part.board == Board::La;
		const bool rhythm = isLaRhythm(index);

		setText(m_root->GetElementById("pePartTitle"), laPartTitle(index));
		const auto channel = la ? m_laMemory.laReceiveChannel(part.part) : m_laMemory.pcmChannel(part.part);
		auto info = channel < 16 ? "MIDI channel " + std::to_string(channel + 1) : std::string("Receives no channel");
		if(!rhythm)
			info += " - " + laPartName(part);
		setText(m_root->GetElementById("pePartInfo"), info);

		// The controls are the part's, the timbre's and the partial's shown, on the rhythm part the key's. A timbre
		// the device has told since gets its controls again, without the notice.
		const auto keys = m_laMemory.entryCount(Area::RhythmSetup);
		if(m_laRhythmKey >= keys)
			m_laRhythmKey = 0;
		const auto timbre = laEditedTimbre();
		std::ostringstream controlsKey;
		controlsKey << index << '/' << (rhythm ? m_laRhythmKey : 0) << '/' << int(m_laTab) << '/'
		            << int(timbre.valid) << int(timbre.area) << '/' << timbre.entry << '/'
		            << (timbre.valid && m_laMemory.entryKnown(timbre.area, timbre.entry));
		// What the page shows with them: the partials' kinds and switches, the PCM bank
		if(timbre.valid)
		{
			controlsKey << '/' << int(m_laMemory.value(timbre.area, timbre.entry, 0x0a)) << '/'
			            << int(m_laMemory.value(timbre.area, timbre.entry, 0x0b)) << '/'
			            << int(m_laMemory.value(timbre.area, timbre.entry, 0x0c));
			if(m_laTab > 0)
			{
				const auto wave = laPartialBase(static_cast<uint8_t>(m_laTab - 1)) + 0x04u;
				controlsKey << '/' << int(m_laMemory.value(timbre.area, timbre.entry, wave));
			}
		}
		if(controlsKey.str() != m_laControlsKey)
		{
			// Not under the click that changed it: the page goes and comes again from the timer
			if(m_laBuildNow)
			{
				m_laControlsKey = controlsKey.str();
				m_laRebuildPending = false;
				buildLaPartControls();
			}
			else
			{
				m_laRebuildPending = true;
			}
		}
		if(m_laKeySelect)
		{
			// A key by its note and the timbre it plays
			std::vector<std::string> names;
			for(size_t k = 0; k < keys; ++k)
			{
				const auto value = m_laMemory.value(Area::RhythmSetup, k, 0);
				const auto name = laTimbreName(value < 64 ? 2 : 3, value & 63);
				const auto note = noteName(static_cast<uint8_t>(g_firstRhythmKey + k));
				names.push_back(note + "  " + (name.empty() ? laTimbreCode(value < 64 ? 2 : 3, value & 63) : name));
			}
			m_updatingUi = true;
			if(names != m_laKeyNames)
			{
				m_laKeySelect->setOptions(names);
				m_laKeyNames = names;
			}
			if(m_laKeySelect->getSelectedIndex() != static_cast<int>(m_laRhythmKey))
				m_laKeySelect->setSelectedIndex(m_laRhythmKey, false);
			m_updatingUi = false;
		}
		refreshLaControls(m_laPartControls);
		refreshLaTimbreEditor();

		// The list has what the part can play: timbres or tones
		if(m_mapSelect)
			juceRmlUi::helper::setVisible(m_mapSelect, false);
		setText(m_root->GetElementById("peListLabel"), rhythm ? "Timbre of the key" : la ? "Timbre" : "Tone");

		// The list is built again when what it shows changes: the part, the filter, the names
		auto rows = laListRows();
		std::ostringstream key;
		key << index << '/' << m_filter << '/';
		for(const auto& row : rows)
			key << row.heading << int(row.group) << ':' << int(row.number) << row.label << '=' << row.name << '|';
		if(key.str() != m_listKey)
		{
			m_listKey = key.str();
			m_listSelection.clear();
			m_laListRows = std::move(rows);
			m_laListElements.clear();
			auto* list = m_root->GetElementById("peTones");
			std::ostringstream rml;
			bool any = false;
			for(size_t r = 0; r < m_laListRows.size(); ++r)
			{
				const auto& row = m_laListRows[r];
				if(row.heading)
				{
					rml << "<div class=\"peListHeading\">" << encode(row.name) << "</div>";
					continue;
				}
				any = true;
				rml << "<div id=\"peLa" << r << "\" class=\"peItem\"><span class=\"peItemNumber\">" << encode(row.label)
				    << "</span>" << encode(row.name) << "</div>";
			}
			if(!any)
				rml << "<div class=\"peListEmpty\">Nothing matches the filter</div>";
			if(list)
			{
				list->SetInnerRML(rml.str());
				for(size_t r = 0; r < m_laListRows.size(); ++r)
				{
					const auto id = "peLa" + std::to_string(r);
					auto* element = m_laListRows[r].heading ? nullptr : list->GetElementById(id);
					m_laListElements.push_back(element);
					if(element)
						juceRmlUi::EventListener::Add(element, Rml::EventId::Mousedown,
							[this, row = m_laListRows[r]](Rml::Event& _event)
						{
							if(_event.GetParameter<int>("button", 0) != 0)
								return;
							selectLaListRow(row);
							auditionPart(selectedPart());
						});
				}
			}
		}
		applyLaListSelection();
	}

	void PartEditorWindow::setLaValue(const uint32_t _address, const std::vector<uint8_t>& _data)
	{
		m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [&](const PendingValue& _v)
		{
			return _v.mt32 && _v.address == _address && _v.data.size() == _data.size();
		}), m_pending.end());
		m_pending.push_back({0, _address, _data, true});
	}

	void PartEditorWindow::sendNextLaValue()
	{
		// One at a time, each with the room the board needs after it: an edit dragged along a slider or typed into a
		// name goes out as fast as the board takes it, its last value at the end
		if(m_laFlushWait)
		{
			--m_laFlushWait;
			return;
		}
		if(m_pending.empty())
			return;
		const auto value = m_pending.front();
		m_pending.erase(m_pending.begin());
		const auto message = value.mt32 ? emu88Lib::mt32::dt1(value.address, value.data)
		                                : gs::dt1(value.address, value.data);
		synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
		event.sysex.assign(message.begin(), message.end());
		event.port = value.port;
		m_processor.sendMidiEvents({event});
		bool pcmPatchMemory = true;
		m_laFlushWait = laPauseTicks(message, pcmPatchMemory);
		if(m_laFlushWait)
			--m_laFlushWait;
	}

	void PartEditorWindow::updateLaListeners()
	{
		// One MIDI IN: the processor takes every message as port A's
		emu88Lib::PartActivity::Listeners listeners{};
		for(size_t i = 0; i < m_laParts.size() && i < 64; ++i)
		{
			const auto& part = m_laParts[i];
			const auto channel = part.board == emu88Lib::mt32::Board::La ? m_laMemory.laReceiveChannel(part.part)
			                                                             : m_laMemory.pcmChannel(part.part);
			if(channel < 16)
				listeners[channel] |= uint64_t{1} << i;
		}
		m_processor.setLaPartListeners(listeners);
	}

	std::string PartEditorWindow::laControlRows(const emu88Lib::mt32::Area _area, const size_t _entry,
		const emu88Lib::mt32::Block _block, const uint16_t _base, const std::string& _section,
		const std::vector<std::string>& _skip, const std::vector<std::string>& _only) const
	{
		using namespace emu88Lib::mt32;
		const auto& list = params(_block);
		std::ostringstream rml;
		for(size_t i = 0; i < list.size(); ++i)
		{
			const auto& p = list[i];
			if(_section != p.section || std::find(_skip.begin(), _skip.end(), p.id) != _skip.end() ||
			   (!_only.empty() && std::find(_only.begin(), _only.end(), p.id) == _only.end()))
				continue;
			// A firmware bug: the CM-32P's check after a write to its system area clears these
			const bool cleared = _area == Area::PcmSystem && p.offset > 4 && p.offset < 4 + g_pcmParts;
			rml << controlRow(laControlId(_area, _entry, _base, i), p.label, isLaChoice(p, m_laMemory.firmware()), 0,
				maxValue(p, m_laMemory.firmware()), false,
				cleared ? "The CM-32P clears this at any write to its system area, of whatever setting." : "");
		}
		return rml.str();
	}

	void PartEditorWindow::wireLaControls(Rml::Element* _container, const emu88Lib::mt32::Area _area,
		const size_t _entry, const emu88Lib::mt32::Block _block, const uint16_t _base,
		std::vector<LaControl>& _controls)
	{
		using namespace emu88Lib::mt32;
		const auto& list = params(_block);
		const auto firmware = m_laMemory.firmware();
		for(size_t i = 0; i < list.size(); ++i)
		{
			const auto id = laControlId(_area, _entry, _base, i);
			auto* element = _container->GetElementById("peCtl" + id);
			if(!element)
				continue;
			const auto& param = list[i];
			const auto offset = static_cast<uint16_t>(_base + param.offset);
			LaControl control{_area, _entry, _block, _base, i};
			control.slider = dynamic_cast<Rml::ElementFormControlInput*>(element);
			control.combo = dynamic_cast<juceRmlUi::ElemComboBox*>(element);
			control.value = _container->GetElementById("peVal" + id);

			const auto reset = [this, _area, _entry, _block, _base, i, offset]
			{
				m_dragging = nullptr;
				setLaParam(_area, _entry, _block, _base, i, laDefault(_area, _entry, offset));
			};
			if(auto* combo = control.combo)
			{
				// A structure by what it does, a PCM wave by its sample in the partial's bank
				std::vector<std::pair<uint16_t, std::string>> choices;
				for(const auto& text : valueList(param, firmware))
				{
					const auto v = static_cast<uint8_t>(choices.size());
					if(param.text == ParamText::Structure)
						choices.emplace_back(v, std::to_string(v + 1) + "   " +
							structureName(v, param.offset == 0x0a ? 1 : 3));
					else if(param.text == ParamText::PcmWave)
						choices.emplace_back(v, number(v + 1) + "  " +
							pcmWaveName(m_laMemory.value(_area, _entry, _base + 0x04u) >> 1 & 1, v));
					else
						choices.emplace_back(v, text);
				}
				combo->setEntries(comboEntries(choices));
				juceRmlUi::EventListener::Add(combo, Rml::EventId::Change,
					[this, combo, _area, _entry, _block, _base, i](Rml::Event&)
				{
					if(!m_updatingUi)
						setLaParam(_area, _entry, _block, _base, i, static_cast<uint8_t>(combo->getValue()));
				});
			}
			else if(auto* slider = control.slider)
			{
				juceRmlUi::EventListener::Add(slider, Rml::EventId::Mousedown, [this, slider](Rml::Event&)
				{
					m_dragging = slider;
				});
				const auto max = static_cast<int>(maxValue(param, firmware));
				juceRmlUi::EventListener::Add(slider, Rml::EventId::Change,
					[this, slider, _area, _entry, _block, _base, i, offset, max, &param, valueText = control.value](
						Rml::Event&)
				{
					if(m_updatingUi)
						return;
					const auto position = std::clamp(std::atoi(slider->GetValue().c_str()), 0, max);
					const auto value = static_cast<uint8_t>(isLaPan(param) ? max - position : position);
					setLaParam(_area, _entry, _block, _base, i, value);
					// What the board kept: the partial reserves have rules
					setText(valueText, formatValue(param, m_laMemory.value(_area, _entry, offset)));
				});
				juceRmlUi::EventListener::Add(slider, Rml::EventId::Dblclick, [reset](Rml::Event&) { reset(); });
			}
			else if(element->IsClassSet("peSegments"))
			{
				// A button a value
				for(int c = 0; c < element->GetNumChildren(); ++c)
				{
					auto* button = element->GetChild(c);
					const auto v = static_cast<uint8_t>(button->GetAttribute<int>("value", c));
					control.segments.emplace_back(button, v);
					addClick(button, [this, _area, _entry, _block, _base, i, v]
					{
						setLaParam(_area, _entry, _block, _base, i, v);
					});
				}
			}
			else if(element->IsClassSet("peNum"))
			{
				// A number dragged up and down, scrolled, or double-clicked back to its default
				control.number = element;
				const auto max = static_cast<int>(maxValue(param, firmware));
				// A drag of RmlUi's own: it ends wherever the button comes up, outside the window too
				juceRmlUi::EventListener::Add(element, Rml::EventId::Dragstart,
					[this, control, offset, max](Rml::Event& _event)
				{
					m_laNumberDrag = {true, control, juceRmlUi::helper::getMousePos(_event).y,
						m_laMemory.value(control.area, control.entry, offset), max};
					m_dragging = control.number;
				});
				juceRmlUi::EventListener::Add(element, Rml::EventId::Drag, [this](Rml::Event& _event)
				{
					dragLaNumber(juceRmlUi::helper::getMousePos(_event).y);
				});
				juceRmlUi::EventListener::Add(element, Rml::EventId::Dragend, [this](Rml::Event&)
				{
					m_laNumberDrag.active = false;
					m_dragging = nullptr;
				});
				juceRmlUi::EventListener::Add(element, Rml::EventId::Mousescroll,
					[this, _area, _entry, _block, _base, i, offset, max](Rml::Event& _event)
				{
					const auto delta = _event.GetParameter<float>("wheel_delta_y", 0.0f);
					if(delta == 0.0f)
						return;
					_event.StopPropagation();
					const auto current = static_cast<int>(m_laMemory.value(_area, _entry, offset));
					const auto value = std::clamp(current + (delta < 0 ? 1 : -1), 0, max);
					setLaParam(_area, _entry, _block, _base, i, static_cast<uint8_t>(value));
				});
				juceRmlUi::EventListener::Add(element, Rml::EventId::Dblclick, [reset](Rml::Event&) { reset(); });
			}
			if(auto* label = _container->GetElementById("peLbl" + id))
				juceRmlUi::EventListener::Add(label, Rml::EventId::Dblclick, [reset](Rml::Event&) { reset(); });
			_controls.push_back(control);
		}
	}

	void PartEditorWindow::buildLaControls()
	{
		using namespace emu88Lib::mt32;
		auto* globals = m_root->GetElementById("peGlobals");
		if(!globals)
			return;

		// Each board's system area by section: its level and tuning, its reverb, the parts' channels and the partials
		// they keep. The CM-64 has both boards, each under its name.
		std::vector<std::pair<Area, const char*>> boards;
		if(m_laMemory.hasLa())
			boards.emplace_back(Area::LaSystem, "LA Board");
		if(m_laMemory.hasPcm())
			boards.emplace_back(Area::PcmSystem, "PCM Board");

		std::ostringstream rml;
		for(size_t b = 0; b < boards.size(); ++b)
		{
			const auto area = boards[b].first;
			if(boards.size() > 1)
				rml << "<div class=\"peBoardTitle" << (b ? " next" : "") << "\">" << boards[b].second << "</div>";
			std::vector<std::string> sections = {"System", "Reverb", "MIDI Channel", "Partial Reserve"};
			for(const auto& p : params(blockOf(area)))
			{
				if(std::find(sections.begin(), sections.end(), p.section) == sections.end())
					sections.emplace_back(p.section);
			}
			for(const auto& section : sections)
			{
				const auto rows = laControlRows(area, 0, blockOf(area), 0, section);
				if(!rows.empty())
					rml << "<div class=\"peSection\"><div class=\"peSectionTitle\">" << encode(section) << "</div>"
					    << rows << "</div>";
			}
		}
		globals->SetInnerRML(rml.str());
		for(const auto& [area, title] : boards)
			wireLaControls(globals, area, 0, blockOf(area), 0, m_laControls);
	}

	void PartEditorWindow::refreshLaControls(std::vector<LaControl>& _controls)
	{
		using namespace emu88Lib::mt32;
		m_updatingUi = true;
		for(const auto& control : _controls)
		{
			const auto& param = params(control.block)[control.param];
			const auto offset = static_cast<size_t>(control.base + param.offset);
			const auto value = m_laMemory.value(control.area, control.entry, offset);
			// A byte the device has not told yet
			const bool known = m_laMemory.known(control.area, control.entry, offset);
			if(control.number)
			{
				control.number->SetClass("unknown", !known);
				setText(control.number, known ? formatValue(param, value) : std::string("?"));
			}
			else if(!control.segments.empty())
			{
				for(const auto& [button, v] : control.segments)
					button->SetPseudoClass("checked", known && v == value);
			}
			else if(control.combo)
			{
				control.combo->SetClass("unknown", !known);
				if(static_cast<int>(control.combo->getValue()) != value)
					control.combo->setValue(value, false);
			}
			else if(control.slider && control.slider != m_dragging)
			{
				const auto max = maxValue(param, m_laMemory.firmware());
				const auto text = std::to_string(isLaPan(param) ? max - std::min(value, max) : value);
				control.slider->SetClass("unknown", !known);
				if(control.slider->GetValue() != text)
					control.slider->SetValue(text);
				setText(control.value, known ? formatValue(param, value) : std::string("?"));
			}
		}
		m_updatingUi = false;
	}

	void PartEditorWindow::setLaParam(const emu88Lib::mt32::Area _area, const size_t _entry,
		const emu88Lib::mt32::Block _block, const uint16_t _base, const size_t _param, const uint8_t _value)
	{
		using namespace emu88Lib::mt32;
		const auto& param = params(_block)[_param];
		const auto at = static_cast<uint16_t>(_base + param.offset);
		if(m_laMemory.value(_area, _entry, at) == _value && m_laMemory.known(_area, _entry, at))
			return;

		// The partial reserves go together: the old LA firmware takes them only all nine at once
		auto offset = at;
		std::vector<uint8_t> data = {_value};
		const size_t reserves = _area == Area::LaSystem ? g_laParts : _area == Area::PcmSystem ? g_pcmParts : 0;
		if(reserves && param.offset >= 4 && param.offset < 4 + reserves)
		{
			offset = 4;
			data.clear();
			for(size_t i = 0; i < reserves; ++i)
				data.push_back(4 + i == param.offset ? _value : m_laMemory.value(_area, 0, 4 + i));
		}

		writeLa(_area, _entry, offset, data);
		// A part on another channel lights up for that one
		updateLaListeners();
		refresh();
	}

	void PartEditorWindow::buildControls()
	{
		m_globalControls.clear();
		m_partControls.clear();
		m_efxControls.clear();
		m_efxShownType = -1;
		auto* globals = m_root->GetElementById("peGlobals");
		auto* parts = m_root->GetElementById("pePartControls");
		if(!globals || !parts)
			return;

		const bool supported = m_groupCount > 0;

		// The parameters the board has, by section, in the table's order. The insertion effect's twenty go below its
		// type, in rows of their own that follow the type.
		const auto sections = [&](const std::vector<gs::Param>& _params, const bool _global, const int _column)
		{
			std::ostringstream rml;
			const char* section = nullptr;
			for(size_t i = 0; i < _params.size(); ++i)
			{
				const auto& p = _params[i];
				if(!supported || !gs::isAvailable(p, m_model) || p.text == gs::ParamText::EfxParam ||
				   (_column >= 0 && sectionColumn(p.section) != _column))
					continue;
				if(!section || std::strcmp(section, p.section) != 0)
				{
					if(section)
						rml << "</div>";
					section = p.section;
					rml << "<div class=\"peSection\"><div class=\"peSectionTitle\">" << encode(p.section) << "</div>";
				}
				const auto [min, max] = gs::valueRange(p, m_model);
				// The SC-55 and SC-155 store a cutoff of +16 at most, whatever they are sent
				const auto note = p.nrpnMsb == 1 && max < 0x72
					? "This board takes up to " + gs::formatValue(p, max) + " only." : std::string();
				rml << controlRow(controlId(_global, i), p.label, !gs::valueChoices(p).empty(), min, max,
					!gs::hasAddress(p, m_model), note);
				if(p.text == gs::ParamText::EfxType)
					rml << "<div id=\"peEfxParams\" class=\"peEfxParams\"/>";
			}
			if(section)
				rml << "</div>";
			return rml.str();
		};

		globals->SetInnerRML(sections(gs::globalParams(), true, -1));
		parts->SetInnerRML("<div class=\"peColumns\"><div class=\"peColumn\">" + sections(gs::partParams(), false, 0) +
			"</div><div class=\"peColumn\">" + sections(gs::partParams(), false, 1) + "</div></div>");

		for(size_t i = 0; i < gs::globalParams().size(); ++i)
		{
			if(globals->GetElementById("peCtl" + controlId(true, i)))
				m_globalControls.push_back(makeControl(globals, true, i, nullptr));
		}
		for(size_t i = 0; i < gs::partParams().size(); ++i)
		{
			if(parts->GetElementById("peCtl" + controlId(false, i)))
				m_partControls.push_back(makeControl(parts, false, i, nullptr));
		}
		buildEfxControls();
	}

	void PartEditorWindow::buildEfxControls()
	{
		m_efxControls.clear();
		auto* container = m_root->GetElementById("peEfxParams");
		const auto typeParam = gs::findGlobalParamById("EfxType");
		const auto firstParam = gs::findGlobalParamById("EfxParam1");
		if(!container || typeParam < 0 || firstParam < 0)
		{
			m_efxShownType = -1;
			return;
		}
		const auto typeId = m_setup.globals[static_cast<size_t>(typeParam)];
		m_efxShownType = typeId;
		const auto& type = gs::findEfxType(typeId);
		const auto params = gs::efxParams(type);

		std::ostringstream rml;
		for(const auto* p : params)
		{
			rml << controlRow(controlId(true, static_cast<size_t>(firstParam) + p->index), p->name,
				!gs::efxValueList(*p).empty(), p->min, p->max, false);
		}
		container->SetInnerRML(rml.str());
		for(const auto* p : params)
			m_efxControls.push_back(makeControl(container, true, static_cast<size_t>(firstParam) + p->index, p));
	}

	PartEditorWindow::Control PartEditorWindow::makeControl(Rml::Element* _container, const bool _global,
		const size_t _param, const gs::EfxParam* _efx)
	{
		const auto id = controlId(_global, _param);
		auto* element = _container->GetElementById("peCtl" + id);
		Control control;
		control.global = _global;
		control.param = _param;
		control.efx = _efx;
		control.slider = dynamic_cast<Rml::ElementFormControlInput*>(element);
		control.combo = dynamic_cast<juceRmlUi::ElemComboBox*>(element);
		control.value = _container->GetElementById("peVal" + id);

		const auto apply = [this, _global, _param](const uint16_t _value)
		{
			if(_global)
				setGlobalParam(_param, _value);
			else if(m_page != g_mixerPage)
				setPartParam(selectedPart(), _param, _value);
		};
		const auto& param = _global ? gs::globalParams()[_param] : gs::partParams()[_param];

		if(auto* combo = control.combo)
		{
			combo->setEntries(comboEntries(_efx ? efxChoices(*_efx) : gs::valueChoices(param)));
			juceRmlUi::EventListener::Add(combo, Rml::EventId::Change, [this, combo, apply](Rml::Event&)
			{
				if(!m_updatingUi)
					apply(static_cast<uint16_t>(combo->getValue()));
			});
		}
		else if(auto* slider = control.slider)
		{
			juceRmlUi::EventListener::Add(slider, Rml::EventId::Mousedown, [this, slider](Rml::Event&)
			{
				m_dragging = slider;
			});
			const auto range = _efx ? std::pair<uint16_t, uint16_t>(_efx->min, _efx->max) : gs::valueRange(param,
				m_model);
			juceRmlUi::EventListener::Add(slider, Rml::EventId::Change,
				[this, slider, apply, &param, _efx, range, valueText = control.value](Rml::Event&)
			{
				if(m_updatingUi)
					return;
				const auto value = static_cast<uint16_t>(std::clamp(std::atoi(slider->GetValue().c_str()),
					static_cast<int>(range.first), static_cast<int>(range.second)));
				apply(value);
				setText(valueText, _efx ? gs::formatEfxValue(*_efx,
					static_cast<uint8_t>(value)) : gs::formatValue(param, value));
			});
			juceRmlUi::EventListener::Add(slider, Rml::EventId::Dblclick, [this, _global, _param, _efx](Rml::Event&)
			{
				resetControl(_global, _param, _efx);
			});
		}
		// The label resets any control: a choice opens its list with the first click
		if(auto* label = _container->GetElementById("peLbl" + id))
		{
			juceRmlUi::EventListener::Add(label, Rml::EventId::Dblclick, [this, _global, _param, _efx](Rml::Event&)
			{
				resetControl(_global, _param, _efx);
			});
		}
		return control;
	}

	void PartEditorWindow::refreshControls(std::vector<Control>& _controls, const gs::Part* _part)
	{
		m_updatingUi = true;
		for(const auto& control : _controls)
		{
			if(!control.global && !_part)
				continue;
			const auto& param = control.global ? gs::globalParams()[control.param] : gs::partParams()[control.param];
			const uint16_t value = control.global ? m_setup.globals[control.param] : _part->value(control.param);
			if(control.combo)
			{
				if(static_cast<int>(control.combo->getValue()) != value)
					control.combo->setValue(value, false);
			}
			else if(control.slider && control.slider != m_dragging)
			{
				const auto text = std::to_string(value);
				if(control.slider->GetValue() != text)
					control.slider->SetValue(text);
				setText(control.value, control.efx ? gs::formatEfxValue(*control.efx, static_cast<uint8_t>(value))
				                                   : gs::formatValue(param, value));
			}
		}
		m_updatingUi = false;
	}

	uint16_t PartEditorWindow::defaultValue(const bool _global, const size_t _param, const gs::EfxParam* _efx) const
	{
		if(_efx)
		{
			const auto typeParam = gs::findGlobalParamById("EfxType");
			const auto type = typeParam >= 0 ? m_setup.globals[static_cast<size_t>(typeParam)] : uint16_t(0);
			return gs::findEfxType(type).defaults[_efx->index];
		}
		if(_global)
			return m_defaults.globals[_param];
		const auto part = selectedPart();
		return part < m_defaults.parts.size() ? m_defaults.parts[part].value(_param) : gs::partParams()[_param].def;
	}

	void PartEditorWindow::resetControl(const bool _global, const size_t _param, const gs::EfxParam* _efx)
	{
		const auto value = defaultValue(_global, _param, _efx);
		m_dragging = nullptr;
		if(_global)
		{
			setGlobalParam(_param, value);
			refreshMixer();
		}
		else if(m_page != g_mixerPage)
		{
			setPartParam(selectedPart(), _param, value);
		}
		refresh();
	}

	void PartEditorWindow::setMixerValue(const size_t _row, const bool _volume, const uint8_t _value)
	{
		if(_row >= m_mixerRows.size() || mixerValue(_row, _volume) == _value)
			return;
		// Shown at once; the board's echo through the processor's copy confirms it a moment later
		if(m_laDialect)
		{
			const auto address = emu88Lib::mt32::address(laMixerLocation(m_laParts[_row], _volume));
			const auto message = emu88Lib::mt32::dt1(address, std::vector<uint8_t>{_value});
			m_laMemory.processSysex(message.data(), message.size());
			setLaValue(address, {_value});
		}
		else
		{
			auto& p = m_setup.parts[_row];
			(_volume ? p.level : p.pan) = _value;
			const auto part = static_cast<uint8_t>(_row % gs::g_partsPerGroup);
			setPartValue(_row, gs::partAddress(part, _volume ? gs::PartParam::Level : gs::PartParam::Pan), {_value});
		}
		const auto& row = m_mixerRows[_row];
		setText(_volume ? row.volumeValue : row.panValue, mixerText(_row, _volume, _value));
	}

	void PartEditorWindow::setRxNoteMessage(const size_t _part, const bool _on)
	{
		const auto part = static_cast<uint8_t>(_part % gs::g_partsPerGroup);
		setPartValue(_part, gs::partAddress(part, gs::PartParam::RxNoteMessage), {static_cast<uint8_t>(_on ? 1 : 0)});
		m_setup.parts[_part].rxNoteMessage = _on ? 1 : 0;
	}

	void PartEditorWindow::toggleSolo(const size_t _part)
	{
		if(_part >= m_setup.parts.size() || _part >= 64)
			return;
		m_soloed ^= uint64_t{1} << _part;
		applySolo();
		flushPending();
		refreshMixer();
	}

	void PartEditorWindow::toggleMute(const size_t _part)
	{
		if(_part >= m_setup.parts.size() || _part >= 64)
			return;
		const auto bit = uint64_t{1} << _part;
		// A part the solo muted becomes muted by the user: it stays muted when the solo ends
		if(m_soloMuted & bit)
			m_soloMuted &= ~bit;
		else
			setRxNoteMessage(_part, !m_setup.parts[_part].rxNoteMessage);
		applySolo();
		flushPending();
		refreshMixer();
	}

	void PartEditorWindow::applySolo()
	{
		for(size_t i = 0; i < m_setup.parts.size() && i < 64; ++i)
		{
			const auto bit = uint64_t{1} << i;
			const bool leftOut = m_soloed && !(m_soloed & bit);
			if(leftOut && !(m_soloMuted & bit) && m_setup.parts[i].rxNoteMessage)
			{
				setRxNoteMessage(i, false);
				m_soloMuted |= bit;
			}
			else if(!leftOut && (m_soloMuted & bit))
			{
				setRxNoteMessage(i, true);
				m_soloMuted &= ~bit;
			}
		}
		if(!m_pending.empty())
			flushPending();
	}

	void PartEditorWindow::openPartMenu(const size_t _part, Rml::Event& _event)
	{
		const auto index = _part;
		if(index >= m_setup.parts.size())
			return;
		const auto current = m_setup.parts[index].useForRhythm;

		// Every board takes both drum maps
		m_menu = std::make_shared<juceRmlUi::Menu>();
		constexpr const char* kinds[] = {"Normal Part", "Drum Part, Map 1", "Drum Part, Map 2"};
		for(uint8_t kind = 0; kind < 3; ++kind)
			m_menu->addEntry(kinds[kind], current == kind, [this, index, kind] { setPartType(index, kind); });

		auto* target = _event.GetTargetElement();
		m_menu->openPopupWindow(target, target->GetAbsoluteOffset(Rml::BoxArea::Border),
			target->GetBox().GetSize(Rml::BoxArea::Border));
	}

	void PartEditorWindow::openActionsPanel()
	{
		auto* panel = m_root->GetElementById("peActionsPanel");
		if(!panel)
			return;

		const bool la = m_laDialect;
		const bool supported = la || m_groupCount > 0;
		const bool idle = supported && !m_receiving && !laSending();
		const bool onPart = m_page != g_mixerPage;
		// Of the MT-32 dialect's boards only the LA board answers data requests
		const bool answersAll = la ? m_laMemory.hasLa() : gs::answersDataRequests(m_model);
		const bool answersPart = la ? onPart && selectedPart() < m_laParts.size() &&
		                              m_laParts[selectedPart()].board == emu88Lib::mt32::Board::La
		                            : answersAll;
		const std::string noAnswers = la ? "The CM-32P answers no data requests." : std::string();

		// The part a page shows, by name: on the mixer there is none
		std::string name = "Part";
		if(onPart && la)
		{
			name = laPartTitle(selectedPart());
		}
		else if(onPart)
		{
			const auto part = static_cast<int>(selectedPart() % gs::g_partsPerGroup) + 1;
			name += m_groupCount > 1 ? " " + groupLetter(m_group) + number(part, 2) : " " + std::to_string(part);
		}

		// A MIDI file is one track, which reaches part groups A and B only. The MT-32 dialect's boards have one MIDI
		// IN.
		const bool reachable = la || selectedPart() / gs::g_partsPerGroup < 2;
		const std::string single = "A MIDI file is one track, for port A: it reaches part groups A and B only.";

		struct Row
		{
			const char* id;
			std::string text;
			bool enabled;
			std::function<void()> action;
			bool exported = false;
			Export file = Export::Part;
			std::string title = {};
			// A setting rather than an action: it shows its state and keeps the panel open
			bool toggle = false;
			bool on = false;
		};
		const auto sendPart = [this]
		{
			if(m_laDialect)
				laSend(laPartMessages(selectedPart()), "Sent the part to the device.");
			else
				sendParts(selectedPart(), 1, false);
		};
		const auto receivePart = [this]
		{
			if(m_laDialect)
				laReceive(laPartRanges(selectedPart()));
			else
				receiveParts(selectedPart(), 1, false);
		};
		const auto sendAll = [this]
		{
			// Everything: an all parameters reset, then what differs from it
			if(m_laDialect)
				laSend(m_laMemory.createRestore(), "Sent everything to the device.");
			else
				sendParts(0, m_setup.parts.size(), true);
		};
		const auto receiveAll = [this]
		{
			if(m_laDialect)
				laReceive(laAllRanges());
			else
				receiveParts(0, m_setup.parts.size(), true);
		};
		const Row rows[] = {
			{"peActSendPart", "Send " + name, idle && onPart, sendPart},
			{"peActReceivePart", "Receive " + name, idle && onPart && answersPart, receivePart, false, Export::Part,
				onPart && !answersPart ? noAnswers : std::string()},
			{nullptr, {}, false, {}},
			{"peActSendAll", "Send All", idle, sendAll},
			{"peActReceiveAll", "Receive All", idle && answersAll, receiveAll, false, Export::Part,
				la && m_laMemory.hasPcm() ? (answersAll ? "The LA board's settings. " : "") + noAnswers
				                          : std::string()},
			{nullptr, {}, false, {}},
			{"peActSavePart", "Save " + name + " as MIDI...", supported && onPart && reachable,
				[this] { saveMidiFile(Export::Part); }, true, Export::Part, onPart && !reachable ? single : std::string()},
			{"peActSaveGlobals", la ? "Save System as MIDI..." : "Save System and Effects as MIDI...", supported,
				[this] { saveMidiFile(Export::Globals); }, true, Export::Globals},
			{"peActSaveAll", "Save All as MIDI...", supported, [this] { saveMidiFile(Export::All); }, true, Export::All,
				!la && m_groupCount > 2 ? single : std::string()},
			{nullptr, {}, false, {}},
			{"peActAudition", "Audition on Click", true, [this]
				{
					m_audition = !m_audition;
					m_processor.config().setValue("partEditorAudition", m_audition);
					m_processor.config().saveIfNeeded();
					stopAudition();
				}, false, Export::Part, "Picking from the list plays a note on the part while the mouse button is down",
				true, m_audition},
		};

		// The grips go with the rows they belong to
		m_exportSources.clear();
		std::ostringstream rml;
		for(const auto& row : rows)
		{
			if(!row.id)
			{
				rml << "<div class=\"peActionsSeparator\"/>";
				continue;
			}
			rml << "<div id=\"" << row.id << "\" class=\"peActionsRow" << (row.enabled ? "" : " disabled") << "\"";
			if(!row.title.empty())
				rml << " title=\"" << encode(row.title) << "\"";
			rml << ">";
			if(row.exported)
			{
				rml << "<div id=\"" << row.id << "Grip\" class=\"peGrip\" title=\"Drag to a MIDI track of your host\">"
				    << "<svg class=\"peGripIcon\" src=\"part_editor_grip.svg\"/></div>";
			}
			else if(row.toggle)
			{
				rml << "<div class=\"peGripSpace\"><div id=\"" << row.id << "Check\" class=\"peCheck"
				    << (row.on ? " on" : "") << "\"/></div>";
			}
			else
			{
				rml << "<div class=\"peGripSpace\"/>";
			}
			rml << "<div class=\"peActionsLabel\">" << encode(row.text) << "</div></div>";
		}
		panel->SetInnerRML(rml.str());

		for(const auto& row : rows)
		{
			if(!row.id || !row.enabled)
				continue;
			auto* element = panel->GetElementById(row.id);
			auto* label = element ? element->GetLastChild() : nullptr;
			if(row.toggle)
			{
				auto* check = panel->GetElementById(std::string(row.id) + "Check");
				addClick(element, [this, action = row.action, check]
				{
					action();
					if(check)
						check->SetClass("on", m_audition);
				});
				continue;
			}
			// A save row is clicked on its label, its grip is dragged
			addClick(row.exported ? label : element, [this, action = row.action]
			{
				closeActionsPanel();
				action();
			});
			if(auto* grip = row.exported ? panel->GetElementById(std::string(row.id) + "Grip") : nullptr)
			{
				m_exportSources.push_back(std::make_unique<ExportDragSource>(grip, [this, file = row.file]
				{
					return writeExportFile(file);
				}));
			}
		}
		juceRmlUi::helper::setVisible(panel, true);
		if(auto* button = m_root->GetElementById("peActionsMenu"))
			button->SetPseudoClass("checked", true);
	}

	void PartEditorWindow::closeActionsPanel()
	{
		juceRmlUi::helper::setVisible(m_root->GetElementById("peActionsPanel"), false);
		if(auto* button = m_root->GetElementById("peActionsMenu"))
			button->SetPseudoClass("checked", false);
	}

	bool PartEditorWindow::isActionsPanelOpen() const
	{
		const auto* panel = m_root ? m_root->GetElementById("peActionsPanel") : nullptr;
		return panel && panel->IsVisible();
	}

	std::string PartEditorWindow::exportName(const Export _export) const
	{
		const std::string device = emu88Lib::getDeviceProfile(m_model).displayName;
		switch(_export)
		{
		case Export::Part:
			{
				if(m_laDialect)
					return device + " " + laPartTitle(selectedPart());
				const auto part = static_cast<int>(selectedPart() % gs::g_partsPerGroup) + 1;
				return device + " Part " + (m_groupCount > 1 ? groupLetter(m_group) + number(part, 2) : std::to_string(part));
			}
		case Export::Globals:
			return device + (m_laDialect ? " System" : " System and Effects");
		default:
			return device + " Setup";
		}
	}

	std::vector<uint8_t> PartEditorWindow::createMidiFile(const Export _export) const
	{
		gs::MidiClip clip;
		if(m_laDialect)
		{
			// The same messages as a send, with the same room between them
			auto messages = _export == Export::Part ? laPartMessages(selectedPart())
			              : _export == Export::Globals ? laSystemMessages() : m_laMemory.createRestore();
			bool pcmPatchMemory = false;
			for(auto& message : messages)
			{
				const auto pause = laPause(message, pcmPatchMemory);
				clip.add(std::move(message), pause);
			}
			return clip.toSmf(exportName(_export));
		}
		const auto add = [&](const std::vector<synthLib::SMidiEvent>& _group)
		{
			for(const auto& e : _group)
				clip.add(messageBytes(e));
		};

		switch(_export)
		{
		case Export::Part:
			for(auto& group : setupMessages(selectedPart(), 1, false))
			{
				if(const auto single = forPortA(std::move(group)))
					add(*single);
			}
			break;
		case Export::Globals:
			for(const auto& group : setupMessages(0, 0, true))
				add(group);
			break;
		case Export::All:
			{
				// A GS Reset, then what differs from it: a copy of the part state follows the file as the board would,
				// and a setting that changes nothing there stays out
				synthLib::SMidiEvent reset(synthLib::MidiEventSource::Host);
				const auto message = gs::dt1(gs::g_gsReset, 0x00);
				reset.sysex.assign(message.begin(), message.end());
				gs::PartStateTracker board;
				board.setModel(m_model, m_catalog);
				board.process(reset);
				add({reset});
				for(auto& group : setupMessages(0, m_setup.parts.size(), true))
				{
					const auto single = forPortA(std::move(group));
					if(!single)
						continue;
					const auto revision = board.revision();
					for(const auto& e : *single)
						board.process(e);
					if(board.revision() != revision)
						add(*single);
				}
				break;
			}
		}
		return clip.toSmf(exportName(_export));
	}

	std::string PartEditorWindow::writeExportFile(const Export _export) const
	{
		const auto folder = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("88emuPlayer MIDI");
		if(!folder.createDirectory())
			return {};
		const auto file = folder.getChildFile(juce::File::createLegalFileName(exportName(_export)) + ".mid");
		const auto data = createMidiFile(_export);
		if(!file.replaceWithData(data.data(), data.size()))
			return {};
		return file.getFullPathName().toStdString();
	}

	void PartEditorWindow::saveMidiFile(const Export _export)
	{
		const auto name = exportName(_export);
		const auto lastFolder = m_processor.config().getValue("partEditorMidiFolder");
		auto folder = lastFolder.isNotEmpty() ? juce::File(lastFolder)
		                                      : juce::File::getSpecialLocation(juce::File::userMusicDirectory);
		if(!folder.isDirectory())
			folder = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

		genericUI::MessageBox::FileDialogDesc desc;
		desc.title = "Save " + name + " as MIDI";
		desc.initialPath = folder.getChildFile(juce::File::createLegalFileName(name) + ".mid").getFullPathName().toStdString();
		desc.patterns = "*.mid";
		desc.flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
		             juce::FileBrowserComponent::warnAboutOverwriting;

		// What the window shows now. The dialog goes with the window, and its callback with it.
		m_fileDialog = genericUI::MessageBox::showFileDialog(desc,
			[this, name, data = createMidiFile(_export)](const std::vector<std::string>& _files)
		{
			auto file = _files.empty() ? juce::File() : juce::File(_files.front());
			if(file != juce::File())
			{
				if(!file.hasFileExtension("mid") && !file.hasFileExtension("midi"))
					file = file.withFileExtension("mid");
				const bool written = file.replaceWithData(data.data(), data.size());
				if(written)
				{
					m_processor.config().setValue("partEditorMidiFolder", file.getParentDirectory().getFullPathName());
					m_processor.config().saveIfNeeded();
				}
				juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
				showStatus(written ? "Saved " + name + " to " + file.getFileName().toStdString() + "."
				                   : "Could not write " + file.getFullPathName().toStdString() + ".", !written);
				m_rml->enqueueUpdate();
			}
			m_fileDialog.reset();
		});
	}

	void PartEditorWindow::setPartType(const size_t _part, const uint8_t _useForRhythm)
	{
		// Runs from the menu's own window: the board gets it now, the timer shows what comes back
		if(_part >= m_setup.parts.size() || m_setup.parts[_part].useForRhythm == _useForRhythm)
			return;
		const auto part = static_cast<uint8_t>(_part % gs::g_partsPerGroup);
		setPartValue(_part, gs::partAddress(part, gs::PartParam::UseForRhythm), {_useForRhythm});
		flushPending();
	}

	void PartEditorWindow::selectBank(const uint8_t _bank)
	{
		if(_bank >= m_groupCount)
			return;
		// The switch picks parts too: the one in the same place of the other groups, or from the mixer the part it
		// last showed there
		const auto lastGroup = static_cast<uint8_t>(m_lastPart / gs::g_partsPerGroup);
		const auto group = m_page == g_mixerPage && lastGroup / 2 * 2 == _bank
			? lastGroup : static_cast<uint8_t>(_bank + m_group % 2);
		const auto place = m_page == g_mixerPage ? m_lastPart % gs::g_partsPerGroup : static_cast<size_t>(m_page);
		selectPart(static_cast<size_t>(group) * gs::g_partsPerGroup + place);
	}

	void PartEditorWindow::selectPart(const size_t _part)
	{
		// The MT-32 dialect's boards have a page per part, in one row
		if(m_laDialect)
		{
			if(_part < m_laParts.size())
				selectPage(static_cast<int>(_part));
			return;
		}
		if(_part >= m_setup.parts.size())
			return;
		m_group = static_cast<uint8_t>(_part / gs::g_partsPerGroup);
		// The tabs show the part's two groups
		if(m_groupCount > 2 && m_group / 2 * 2 != m_bank)
		{
			m_bank = static_cast<uint8_t>(m_group / 2 * 2);
			labelPartTabs();
		}
		selectPage(static_cast<int>(_part % gs::g_partsPerGroup));
	}

	void PartEditorWindow::selectPage(const int _page)
	{
		const ScopedFlag buildNow(m_laBuildNow);
		stopAudition();
		const int pages = m_laDialect ? static_cast<int>(m_laParts.size()) : m_groupCount ? gs::g_partsPerGroup : 0;
		m_page = pages ? std::clamp(_page, g_mixerPage, pages - 1) : g_mixerPage;
		if(m_page != g_mixerPage)
			m_lastPart = selectedPart();
		m_listKey.clear();

		const bool mixer = m_page == g_mixerPage;
		juceRmlUi::helper::setVisible(m_root->GetElementById("pePageMixer"), mixer);
		juceRmlUi::helper::setVisible(m_root->GetElementById("pePagePart"), !mixer);
		refresh();
	}

	size_t PartEditorWindow::selectedPart() const
	{
		return static_cast<size_t>(m_group) * gs::g_partsPerGroup + static_cast<size_t>(std::max(0, m_page));
	}

	void PartEditorWindow::refresh()
	{
		refreshTabs();
		if(m_page == g_mixerPage)
			refreshMixer();
		else
			refreshPart();
		refreshActions();
	}

	void PartEditorWindow::refreshTabs()
	{
		if(auto* mixer = m_root->GetElementById("peTabMixer"))
			mixer->SetPseudoClass("checked", m_page == g_mixerPage);
		// One selection across the mixer, the group switch and the parts, as radio buttons
		for(size_t b = 0; b < m_groupTabs.size(); ++b)
		{
			if(m_groupTabs[b])
				m_groupTabs[b]->SetPseudoClass("checked", m_page != g_mixerPage && b == m_group / 2u);
		}
		for(size_t p = 0; p < m_partTabs.size(); ++p)
		{
			auto* tab = m_partTabs[p];
			if(!tab)
				continue;
			const auto index = static_cast<size_t>(m_bank) * gs::g_partsPerGroup + p;
			tab->SetPseudoClass("checked", m_page != g_mixerPage && index == selectedPart());
			const bool drum = m_laDialect ? isLaRhythm(index)
			                              : index < m_setup.parts.size() && m_setup.parts[index].isDrumPart();
			tab->SetClass("drum", drum);
			const auto title = m_laDialect ? laPartName(m_laParts[index]) : toneName(index);
			if(tab->GetAttribute<Rml::String>("title", "") != title)
				tab->SetAttribute("title", title);
		}
		refreshActivity();
	}

	void PartEditorWindow::refreshMixer()
	{
		m_updatingUi = true;
		const auto count = m_laDialect ? m_laParts.size() : m_setup.parts.size();
		for(size_t i = 0; i < m_mixerRows.size() && i < count; ++i)
		{
			const auto& row = m_mixerRows[i];
			if(m_laDialect)
			{
				if(row.row)
					row.row->SetClass("drum", isLaRhythm(i));
				setText(row.tone, laPartName(m_laParts[i]));
			}
			else
			{
				const auto& p = m_setup.parts[i];
				const auto bit = uint64_t{1} << (i & 63);
				if(row.row)
				{
					row.row->SetClass("drum", p.isDrumPart());
					row.row->SetClass("silenced", !p.rxNoteMessage);
				}
				// The mute is the board's: the front panel's MUTE, a song's SysEx, the editor's own. One the solo set
				// is the solo's.
				if(row.solo)
					row.solo->SetPseudoClass("checked", (m_soloed & bit) != 0);
				if(row.mute)
					row.mute->SetPseudoClass("checked", !p.rxNoteMessage && !(m_soloMuted & bit));
				setText(row.tone, toneName(i));
			}
			for(const bool volume : {true, false})
			{
				auto* slider = volume ? row.volume : row.pan;
				if(!slider || slider == m_dragging)
					continue;
				const auto value = mixerValue(i, volume);
				const auto position = std::to_string(mixerSlider(i, volume, value));
				if(slider->GetValue() != position)
					slider->SetValue(position);
				setText(volume ? row.volumeValue : row.panValue, mixerText(i, volume, value));
			}
		}
		m_updatingUi = false;
		if(m_laDialect)
		{
			refreshLaControls(m_laControls);
			return;
		}
		refreshControls(m_globalControls, nullptr);
		const auto typeParam = gs::findGlobalParamById("EfxType");
		if(typeParam >= 0 && m_setup.globals[static_cast<size_t>(typeParam)] != m_efxShownType)
			buildEfxControls();
		refreshControls(m_efxControls, nullptr);
	}

	void PartEditorWindow::refreshPart()
	{
		if(m_laDialect)
		{
			refreshLaPart();
			return;
		}
		const auto index = selectedPart();
		if(index >= m_setup.parts.size())
			return;
		const auto& p = m_setup.parts[index];

		const auto part = static_cast<int>(index % gs::g_partsPerGroup) + 1;
		setText(m_root->GetElementById("pePartTitle"), m_groupCount > 1
			? "Part " + groupLetter(m_group) + number(part, 2) : "Part " + std::to_string(part));

		std::string info;
		if(p.rxChannel < 16)
			info = "MIDI channel " + std::to_string(p.rxChannel + 1) + (m_groupCount > 1 ? " on port " + groupLetter(m_group) : "");
		else
			info = "Receives no channel";
		info += p.isDrumPart() ? ", drum part (map " + std::to_string(p.useForRhythm) + ")" : ", normal part";
		info += " - " + toneName(index);
		setText(m_root->GetElementById("pePartInfo"), info);

		// The list changes with the part, its map, its kind and the filter. A tone the map has nothing at gets a row
		// of its own; any other only moves the selection.
		const auto map = m_setup.effectiveToneMap(index);
		const auto* tones = m_catalog ? m_catalog->findMap(map) : nullptr;
		const bool missing = tones && (p.isDrumPart() ? !tones->findDrumSet(p.program) : !tones->find(p.bankMsb, p.program));
		std::ostringstream key;
		key << index << '/' << int(map) << '/' << p.isDrumPart() << '/' << (m_catalog ? m_catalog.get() : nullptr) << '/'
		    << m_filter;
		if(missing)
			key << "/missing " << int(p.bankMsb) << '/' << int(p.program) << '/' << toneName(index);
		if(key.str() != m_listKey)
		{
			m_listKey = key.str();
			buildToneLists();
		}
		applyListSelection();
		refreshMapSelect();
		refreshControls(m_partControls, &p);
	}

	void PartEditorWindow::refreshMapSelect()
	{
		if(!m_mapSelect)
			return;
		const auto index = selectedPart();
		const bool show = m_catalog && gs::hasToneMaps(m_model) && m_catalog->maps.size() > 1
			&& index < m_setup.parts.size();
		juceRmlUi::helper::setVisible(m_mapSelect, show);
		if(!show)
			return;

		std::vector<std::string> names;
		for(const auto& map : m_catalog->maps)
			names.push_back(map.name);
		m_updatingUi = true;
		if(names != m_mapNames)
		{
			m_mapSelect->setOptions(names);
			m_mapNames = names;
		}
		const auto* map = m_catalog->findMap(m_setup.effectiveToneMap(index));
		if(map)
		{
			const auto selected = static_cast<int>(map - m_catalog->maps.data());
			if(m_mapSelect->getSelectedIndex() != selected)
				m_mapSelect->setSelectedIndex(static_cast<size_t>(selected), false);
		}
		m_updatingUi = false;
	}

	bool PartEditorWindow::matchesFilter(const std::string& _name) const
	{
		return m_filter.empty() || lowercase(_name).find(m_filter) != std::string::npos;
	}

	void PartEditorWindow::buildToneLists()
	{
		const auto index = selectedPart();
		if(index >= m_setup.parts.size())
			return;
		const auto& p = m_setup.parts[index];
		const auto mapNumber = m_setup.effectiveToneMap(index);
		const auto* map = m_catalog ? m_catalog->findMap(mapNumber) : nullptr;

		std::ostringstream rml;
		auto* list = m_root->GetElementById("peTones");
		m_listRows.clear();
		m_listSelection.clear();

		// The rows are given their selection afterwards, by applyListSelection()
		std::vector<std::pair<std::string, ListRow>> rows;
		const auto addItem = [&](const std::string& _id, const std::string& _classes, const std::string& _number,
			const std::string& _name, const ListRow& _row)
		{
			rml << "<div id=\"" << _id << "\" class=\"peItem " << _classes << "\"><span class=\"peItemNumber\">"
			    << _number << "</span>" << encode(_name) << "</div>";
			rows.emplace_back(_id, _row);
		};
		const auto noMatch = [&] { rml << "<div class=\"peListEmpty\">Nothing matches the filter</div>"; };

		if(p.isDrumPart())
		{
			setText(m_root->GetElementById("peListLabel"), "Drum Set");

			std::vector<std::pair<uint8_t, std::string>> sets;
			if(map && !map->drumSets.empty())
			{
				for(const auto& set : map->drumSets)
					sets.emplace_back(set.program, set.name);
			}
			else
			{
				for(int prog = 0; prog < 128; ++prog)
					sets.emplace_back(static_cast<uint8_t>(prog), "Drum set " + number(prog + 1));
			}

			bool any = false;
			for(const auto& [program, name] : sets)
			{
				if(!matchesFilter(name))
					continue;
				addItem("peSet" + std::to_string(program), "", number(program + 1), name, {nullptr, program, 0, true});
				any = true;
			}
			// A program the map has nothing at: what the board makes of it
			if(map && !map->findDrumSet(p.program))
				rml << "<div class=\"peItem selected missing\"><span class=\"peItemNumber\">" << number(p.program + 1)
				    << "</span>" << encode(toneName(index)) << "</div>";
			else if(!any)
				noMatch();
		}
		else
		{
			setText(m_root->GetElementById("peListLabel"), "Instrument");

			std::vector<std::vector<const gs::Tone*>> byProgram(128);
			if(map)
			{
				for(const auto& tone : map->tones)
					byProgram[tone.program & 0x7f].push_back(&tone);
			}

			// One row per instrument, named after its first tone, the capital one where the map has it, with its
			// other tones under it. A filter keeps the instruments with a tone that matches, and of their variations
			// the ones that match.
			bool any = false;
			for(int prog = 0; prog < 128; ++prog)
			{
				const auto program = static_cast<uint8_t>(prog);
				const auto& tones = byProgram[prog];
				const auto* capital = tones.empty() ? nullptr : tones.front();
				const auto name = capital ? capital->name : (map ? std::string("-") : "Program " + number(prog + 1));

				std::vector<const gs::Tone*> variations;
				for(size_t t = 1; t < tones.size(); ++t)
				{
					if(matchesFilter(tones[t]->name))
						variations.push_back(tones[t]);
				}
				if(!matchesFilter(name) && variations.empty())
					continue;
				any = true;

				// With a filter, an instrument only listed for its variations is there for context
				addItem("peInst" + std::to_string(prog), std::string("peInstrument") + (matchesFilter(name) ? "" : " context"),
					number(prog + 1), name, {nullptr, program, static_cast<uint8_t>(capital ? capital->bankMsb : 0), true});
				for(const auto* tone : variations)
				{
					addItem("peVar" + std::to_string(prog) + "_" + std::to_string(tone->bankMsb), "peVariation",
						number(tone->bankMsb), tone->name, {nullptr, program, tone->bankMsb, false});
				}
				// A variation the map has nothing at: what the board makes of it, a substitute, a user tone or nothing
				if(map && program == p.program && !map->find(p.bankMsb, p.program))
					rml << "<div class=\"peItem peVariation selected missing\"><span class=\"peItemNumber\">"
					    << number(p.bankMsb) << "</span>" << encode(toneName(index)) << "</div>";
			}
			if(!any)
				noMatch();
		}
		list->SetInnerRML(rml.str());

		for(auto& [id, row] : rows)
		{
			row.element = list->GetElementById(id);
			if(!row.element)
				continue;
			const auto program = row.program;
			const auto bank = row.bankMsb;
			const bool drum = p.isDrumPart();
			// Picked on the press, which plays it until the release
			juceRmlUi::EventListener::Add(row.element, Rml::EventId::Mousedown,
				[this, index, program, bank, drum](Rml::Event& _event)
			{
				if(_event.GetParameter<int>("button", 0) != 0)
					return;
				selectTone(index, drum ? 0 : bank, program);
				auditionPart(index);
			});
			m_listRows.push_back(row);
		}
	}

	void PartEditorWindow::applyListSelection()
	{
		const auto index = selectedPart();
		if(index >= m_setup.parts.size())
			return;
		const auto& p = m_setup.parts[index];

		const auto selection = std::to_string(p.program) + '/' + std::to_string(p.isDrumPart() ? 0 : p.bankMsb);
		if(selection == m_listSelection)
			return;
		m_listSelection = selection;

		Rml::Element* selected = nullptr;
		for(const auto& row : m_listRows)
		{
			const bool isSelected = row.program == p.program && (p.isDrumPart() || row.bankMsb == p.bankMsb);
			row.element->SetClass("selected", isSelected);
			// The instrument whose variation the part plays
			row.element->SetClass("current", row.instrument && row.program == p.program && !isSelected);
			if(isSelected)
				selected = row.element;
		}
		if(!selected)
			selected = m_root->GetElementById("peTones") ? m_root->GetElementById("peTones")->QuerySelector(".missing") : nullptr;

		// A selection that came from elsewhere is brought into view, by as little as it takes. A list built this very
		// moment has no layout yet to scroll by.
		if(selected)
		{
			selected->GetOwnerDocument()->UpdateDocument();
			selected->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest,
				Rml::ScrollAlignment::Nearest, Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest));
		}
	}

	void PartEditorWindow::refreshActions()
	{
		// What the menu offers is worked out when it opens
		const bool supported = m_laDialect || m_groupCount > 0;
		auto* menu = m_root->GetElementById("peActionsMenu");
		setActionEnabled(menu, supported);
		const bool answers = m_laDialect ? m_laMemory.hasLa() : gs::answersDataRequests(m_model);
		const std::string title = !supported || answers
			? "Send the settings to the device, receive them from it, or save them as MIDI"
			: "Send the settings to the device, or save them as MIDI. It answers no data requests.";
		if(menu && menu->GetAttribute<Rml::String>("title", "") != title)
			menu->SetAttribute("title", title);
	}

	void PartEditorWindow::showStatus(const std::string& _text, const bool _error)
	{
		m_status = _text;
		m_statusError = _error;
		if(auto* status = m_root->GetElementById("peStatus"))
		{
			setText(status, _text);
			status->SetClass("error", _error);
		}
	}

	void PartEditorWindow::setValue(const uint8_t _port, const uint32_t _address, const std::vector<uint8_t>& _data)
	{
		m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [&](const PendingValue& _v)
		{
			return _v.port == _port && _v.address == _address;
		}), m_pending.end());
		m_pending.push_back({_port, _address, _data});
	}

	void PartEditorWindow::setPartValue(const size_t _part, const uint32_t _address, const std::vector<uint8_t>& _data)
	{
		setValue(static_cast<uint8_t>(_part / gs::g_partsPerGroup), _address, _data);
	}

	void PartEditorWindow::setGlobalParam(const size_t _param, const uint16_t _value)
	{
		const auto& params = gs::globalParams();
		if(_param >= params.size() || m_setup.globals[_param] == _value)
			return;
		const auto& param = params[_param];
		m_setup.globals[_param] = _value;
		// The system and effect parameters go to the first port: the SC-88s take them there only
		setValue(0, param.address, globalData(param, _value));

		// A macro sets the rest of its effect, an insertion effect type its parameters and sends: shown at once
		auto preset = gs::macroPreset(param.address, static_cast<uint8_t>(_value));
		if(param.text == gs::ParamText::EfxType)
		{
			const auto& type = gs::findEfxType(_value);
			for(uint32_t i = 0; i < gs::g_efxParamCount; ++i)
				preset.emplace_back(gs::g_efxParamAddress + i, type.defaults[i]);
			for(uint32_t i = 0; i < std::size(gs::g_efxSendDefaults); ++i)
				preset.emplace_back(0x400317 + i, gs::g_efxSendDefaults[i]);
		}
		for(const auto& [address, value] : preset)
		{
			const auto i = gs::findGlobalParam(address);
			if(i >= 0)
				m_setup.globals[static_cast<size_t>(i)] = value;
		}
		if(!preset.empty())
			refreshMixer();
	}

	void PartEditorWindow::setPartParam(const size_t _part, const size_t _param, const uint16_t _value)
	{
		const auto& params = gs::partParams();
		if(_part >= m_setup.parts.size() || _param >= params.size())
			return;
		auto& part = m_setup.parts[_part];
		const auto& param = params[_param];
		if(part.value(_param) == _value)
			return;
		if(param.field == gs::PartField::UseForRhythm)
		{
			setPartType(_part, static_cast<uint8_t>(_value));
			return;
		}

		if(gs::hasAddress(param, m_model))
		{
			// Shown at once; the board's echo through the part state confirms it a moment later
			part.setValue(_param, _value);
			const auto block = static_cast<uint8_t>(_part % gs::g_partsPerGroup);
			setPartValue(_part, gs::partParamAddress(block, param), partData(param, part, _value));
			return;
		}
		// A part that receives no channel cannot be reached this way: the control goes back
		std::vector<synthLib::SMidiEvent> events;
		if(!channelMessages(_part, _param, _value, events))
		{
			refreshControls(m_partControls, &part);
			return;
		}
		part.setValue(_param, _value);
		flushPending();
		m_processor.sendMidiEvents(events);
	}

	bool PartEditorWindow::channelMessages(const size_t _part, const size_t _param, const uint16_t _value,
		std::vector<synthLib::SMidiEvent>& _events) const
	{
		const auto& part = m_setup.parts[_part];
		const auto& param = gs::partParams()[_param];
		if(part.rxChannel >= 16)
			return false;
		const auto port = static_cast<uint8_t>(_part / gs::g_partsPerGroup);
		const auto cc = [&](const uint8_t _cc, const uint8_t _v)
		{
			auto& event = _events.emplace_back(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(synthLib::M_CONTROLCHANGE | part.rxChannel), _cc, _v);
			event.port = port;
		};
		const auto value = static_cast<uint8_t>(_value);
		if(param.cc != 0xff)
		{
			cc(param.cc, value);
			return true;
		}
		if(param.rpn == 0xff && param.nrpnMsb == 0xff)
			return false;
		if(param.rpn != 0xff)
		{
			cc(101, 0);
			cc(100, param.rpn);
		}
		else
		{
			cc(99, param.nrpnMsb);
			cc(98, param.nrpnLsb);
		}
		cc(6, value);
		// Nothing selected after it, so that a stray data entry changes nothing
		cc(101, 127);
		cc(100, 127);
		return true;
	}

	void PartEditorWindow::flushPending()
	{
		if(m_pending.empty())
			return;
		std::vector<synthLib::SMidiEvent> events;
		events.reserve(m_pending.size());
		for(const auto& value : m_pending)
		{
			auto& event = events.emplace_back(synthLib::MidiEventSource::Host);
			const auto message = value.mt32 ? emu88Lib::mt32::dt1(value.address, value.data)
			                                : gs::dt1(value.address, value.data);
			event.sysex.assign(message.begin(), message.end());
			event.port = value.port;
		}
		m_pending.clear();
		m_processor.sendMidiEvents(events);
	}

	void PartEditorWindow::selectTone(const size_t _part, const uint8_t _bankMsb, const uint8_t _program)
	{
		if(_part >= m_setup.parts.size())
			return;
		const auto part = static_cast<uint8_t>(_part % gs::g_partsPerGroup);
		setPartValue(_part, gs::partAddress(part, gs::PartParam::ToneNumber), {_bankMsb, _program});
		flushPending();
		auto& p = m_setup.parts[_part];
		p.bankMsb = p.pendingBankMsb = _bankMsb;
		p.program = _program;
		refresh();
	}

	void PartEditorWindow::selectToneMap(const size_t _part, const uint8_t _map)
	{
		if(_part >= m_setup.parts.size())
			return;
		auto& p = m_setup.parts[_part];

		// A tone the new map does not have goes to the instrument's first one there, its capital where it has it
		if(const auto* target = m_catalog ? m_catalog->findMap(_map) : nullptr)
		{
			if(p.isDrumPart())
			{
				if(!target->findDrumSet(p.program))
					p.program = 0;
				p.bankMsb = 0;
			}
			else if(!target->find(p.bankMsb, p.program))
			{
				const auto tones = target->variations(p.program);
				p.bankMsb = tones.empty() ? 0 : tones.front()->bankMsb;
			}
		}
		p.toneMap = p.pendingBankLsb = _map;

		// The map waits for the next tone selection. The SC-88 family takes it with a tone number by SysEx, the
		// SC-8850 only with a program change, which goes to every part on the channel.
		std::vector<synthLib::SMidiEvent> events;
		if(isSc8850(m_model) && programChange(_part, events))
		{
			flushPending();
			m_processor.sendMidiEvents(events);
		}
		else
		{
			const auto part = static_cast<uint8_t>(_part % gs::g_partsPerGroup);
			setPartValue(_part, gs::partAddress(part, gs::PartMapParam::ToneMap), {_map});
			setPartValue(_part, gs::partAddress(part, gs::PartParam::ToneNumber), {p.bankMsb, p.program});
			flushPending();
		}
		refresh();
	}

	bool PartEditorWindow::programChange(const size_t _part, std::vector<synthLib::SMidiEvent>& _events) const
	{
		if(_part >= m_setup.parts.size())
			return false;
		const auto& p = m_setup.parts[_part];
		if(p.rxChannel >= 16)
			return false;
		const auto port = static_cast<uint8_t>(_part / gs::g_partsPerGroup);
		const auto add = [&](const uint8_t _status, const uint8_t _data1, const uint8_t _data2)
		{
			auto& event = _events.emplace_back(synthLib::MidiEventSource::Host,
				static_cast<uint8_t>(_status | p.rxChannel), _data1, _data2);
			event.port = port;
		};
		add(synthLib::M_CONTROLCHANGE, 0, p.bankMsb);
		if(gs::hasToneMaps(m_model))
			add(synthLib::M_CONTROLCHANGE, 32, p.toneMap);
		add(synthLib::M_PROGRAMCHANGE, p.program, 0);
		return true;
	}

	PartEditorWindow::MessageGroups PartEditorWindow::setupMessages(const size_t _first, const size_t _count,
		const bool _withGlobals) const
	{
		const bool maps = gs::hasToneMaps(m_model);
		MessageGroups groups;
		const auto add = [&](const uint8_t _port, const gs::Message& _message)
		{
			auto& event = groups.emplace_back().emplace_back(synthLib::MidiEventSource::Host);
			event.sysex.assign(_message.begin(), _message.end());
			event.port = _port;
		};
		const auto addChannel = [&](std::vector<synthLib::SMidiEvent>&& _events)
		{
			if(!_events.empty())
				groups.push_back(std::move(_events));
		};

		// The system and effects on the first port, in the table's order: a macro before what it sets
		if(_withGlobals)
		{
			const auto& globals = gs::globalParams();
			for(size_t i = 0; i < globals.size(); ++i)
			{
				if(gs::hasAddress(globals[i], m_model))
					add(0, gs::dt1(globals[i].address, globalData(globals[i], m_setup.globals[i])));
			}
		}

		for(size_t i = _first; i < _first + _count && i < m_setup.parts.size(); ++i)
		{
			const auto& p = m_setup.parts[i];
			const auto port = static_cast<uint8_t>(i / gs::g_partsPerGroup);
			const auto part = static_cast<uint8_t>(i % gs::g_partsPerGroup);
			// The part's kind, switches and map before its tone, which they decide the meaning of
			add(port, gs::dt1(gs::partAddress(part, gs::PartParam::RxChannel), p.rxChannel));
			add(port, gs::dt1(gs::partAddress(part, gs::PartParam::RxNoteMessage), p.rxNoteMessage));
			add(port, gs::dt1(gs::partAddress(part, gs::PartParam::UseForRhythm), p.useForRhythm));
			if(gs::hasRxBankSelect(m_model))
				add(port, gs::dt1(gs::partAddress(part, gs::PartParam::RxBankSelect), p.rxBankSelect));
			if(gs::hasKeepTone(m_model))
				add(port, gs::dt1(gs::partAddress(part, gs::PartParam::KeepTone), p.keepTone));
			if(maps)
			{
				add(port, gs::dt1(gs::partAddress(part, gs::PartMapParam::ToneMap0), p.toneMap0));
				add(port, gs::dt1(gs::partAddress(part, gs::PartMapParam::ToneMap), p.toneMap));
			}
			add(port, gs::dt1(gs::partAddress(part, gs::PartParam::ToneNumber), {p.bankMsb, p.program}));
			add(port, gs::dt1(gs::partAddress(part, gs::PartParam::Level), p.level));
			add(port, gs::dt1(gs::partAddress(part, gs::PartParam::Pan), p.pan));

			// The other parameters; those without an address as channel messages
			const auto& params = gs::partParams();
			for(size_t v = 0; v < params.size(); ++v)
			{
				const auto& param = params[v];
				if(param.field != gs::PartField::None || !gs::isAvailable(param, m_model))
					continue;
				if(gs::hasAddress(param, m_model))
				{
					add(port, gs::dt1(gs::partParamAddress(part, param), partData(param, p, p.value(v))));
					continue;
				}
				std::vector<synthLib::SMidiEvent> events;
				channelMessages(i, v, p.value(v), events);
				addChannel(std::move(events));
			}

			// The SC-8850 applies the map with a program change only, which reaches every part on the channel; the
			// parts that have their channel to themselves get one
			if(isSc8850(m_model))
			{
				const auto first = static_cast<size_t>(port) * gs::g_partsPerGroup;
				const auto sharers = std::count_if(m_setup.parts.begin() + static_cast<ptrdiff_t>(first),
					m_setup.parts.begin() + static_cast<ptrdiff_t>(first + gs::g_partsPerGroup),
					[&](const gs::Part& _other) { return _other.rxChannel == p.rxChannel; });
				std::vector<synthLib::SMidiEvent> events;
				if(sharers == 1)
					programChange(i, events);
				addChannel(std::move(events));
			}
		}
		return groups;
	}

	void PartEditorWindow::sendParts(const size_t _first, const size_t _count, const bool _withGlobals)
	{
		flushPending();
		std::vector<synthLib::SMidiEvent> events;
		for(auto& group : setupMessages(_first, _count, _withGlobals))
			events.insert(events.end(), group.begin(), group.end());
		m_processor.sendMidiEvents(events);
		showStatus(_count == 1 ? "Sent the part to the device." : "Sent every part and the effects to the device.");
	}

	void PartEditorWindow::receiveParts(const size_t _first, const size_t _count, const bool _withGlobals)
	{
		flushPending();
		const bool maps = gs::hasToneMaps(m_model);
		std::vector<Processor::DataRequest> requests;
		const auto add = [&](const uint8_t _port, const uint32_t _address, const uint32_t _size)
		{
			Processor::DataRequest request;
			const auto message = gs::rq1(_address, _size);
			request.sysex.assign(message.begin(), message.end());
			request.port = _port;
			request.address = _address;
			requests.push_back(std::move(request));
		};

		if(_withGlobals)
		{
			for(const auto& param : gs::globalParams())
			{
				if(gs::hasAddress(param, m_model))
					add(0, param.address, param.size);
			}
		}

		// One request per parameter: the SC-55s answer nothing that is not exactly one
		for(size_t i = _first; i < _first + _count && i < m_setup.parts.size(); ++i)
		{
			const auto port = static_cast<uint8_t>(i / gs::g_partsPerGroup);
			const auto part = static_cast<uint8_t>(i % gs::g_partsPerGroup);
			for(const auto param : {gs::PartParam::ToneNumber, gs::PartParam::RxChannel, gs::PartParam::RxNoteMessage,
			                        gs::PartParam::UseForRhythm, gs::PartParam::Level, gs::PartParam::Pan})
				add(port, gs::partAddress(part, param), gs::paramSize(param));
			if(gs::hasRxBankSelect(m_model))
				add(port, gs::partAddress(part, gs::PartParam::RxBankSelect), 1);
			if(gs::hasKeepTone(m_model))
				add(port, gs::partAddress(part, gs::PartParam::KeepTone), 1);
			if(maps)
			{
				add(port, gs::partAddress(part, gs::PartMapParam::ToneMap), 1);
				add(port, gs::partAddress(part, gs::PartMapParam::ToneMap0), 1);
			}
			for(const auto& param : gs::partParams())
			{
				if(param.field == gs::PartField::None && gs::hasAddress(param, m_model))
					add(port, gs::partParamAddress(part, param), param.size);
			}
		}

		if(!m_processor.startDataRequests(std::move(requests)))
		{
			showStatus("The device cannot take data requests right now.", true);
			return;
		}
		m_receiving = true;
		showStatus("Receiving...");
		refreshActions();
	}

	void PartEditorWindow::timerCallback()
	{
		if(!m_root)
			return;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);

		if(m_processor.deviceModel() != m_model)
		{
			deviceChanged();
			return;
		}

		const ScopedFlag buildNow(m_laBuildNow);

		// The MT-32 dialect's boards lose SysEx that come too close together: their edits go out one at a time
		if(m_laDialect)
			sendNextLaValue();
		else
			flushPending();

		// A slider or a number still held for a button that is up: let go
		if((m_dragging || m_laNumberDrag.active) && !juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
		{
			m_dragging = nullptr;
			m_laNumberDrag.active = false;
		}

		// A part page that changed under a click of its own comes again now
		bool changed = false;
		if(m_laRebuildPending)
		{
			refresh();
			changed = true;
		}

		if(m_receiving)
		{
			const auto status = m_processor.dataRequestStatus();
			if(status.running)
			{
				showStatus("Receiving " + std::to_string(status.done) + " of " + std::to_string(status.total) + "...");
			}
			else
			{
				m_receiving = false;
				if(!status.error.empty())
					showStatus(status.error, true);
				else if(status.answered < status.total)
					showStatus("Received " + std::to_string(status.answered) + " of " + std::to_string(status.total) +
						(m_laDialect ? " requests" : " values") + "; the device did not answer the rest.", true);
				else if(m_laDialect)
					showStatus("Received the device's settings.");
				else
					showStatus("Received " + std::to_string(status.answered) + " values from the device.");
				refreshActions();
			}
			changed = true;
		}

		// While edits are still queued, the part state does not have them yet and would undo what is shown
		if(m_pending.empty() && m_processor.partState().revision() != m_revision)
		{
			m_setup = m_processor.partState().setup(m_revision);
			// A reset from a song unmutes every part; the solo mutes the ones it leaves out again
			if(m_soloed)
			{
				for(size_t i = 0; i < m_setup.parts.size() && i < 64; ++i)
				{
					if((m_soloMuted >> i) & 1 && m_setup.parts[i].rxNoteMessage)
						m_soloMuted &= ~(uint64_t{1} << i);
				}
				applySolo();
			}
			refresh();
			changed = true;
		}

		// The MT-32 dialect's transfers, paced
		if(!m_laSendQueue.empty())
		{
			if(m_laSendWait)
			{
				--m_laSendWait;
			}
			else
			{
				auto queued = std::move(m_laSendQueue.front());
				m_laSendQueue.pop_front();
				synthLib::SMidiEvent event(synthLib::MidiEventSource::Host);
				event.sysex.assign(queued.message.begin(), queued.message.end());
				m_processor.sendMidiEvents({event});
				m_laSendWait = queued.pauseTicks;
				if(m_laSendQueue.empty())
				{
					showStatus(m_laSendDone);
					refreshActions();
				}
				else
				{
					showStatus("Sending " + std::to_string(m_laSendTotal - m_laSendQueue.size()) + " of " +
						std::to_string(m_laSendTotal) + "...");
				}
			}
			changed = true;
		}

		if(m_laDialect && m_pending.empty() && m_processor.laState().revision() != m_laRevision)
		{
			m_laMemory = m_processor.laState().memory(m_laRevision);
			updateLaListeners();
			refresh();
			changed = true;
		}

		// The parts that play light up, but only while there is a window to see it in, and at a lower rate than the
		// rest: it is nothing the board needs
		const bool shown = isVisible() && !isMinimised();
		changed |= enableActivity(shown);
		if(shown && ++m_activityTick >= g_activityInterval)
		{
			m_activityTick = 0;
			changed |= updateActivity(m_processor.takePartActivity());
		}

		if(changed)
			m_rml->enqueueUpdate();
	}
}
