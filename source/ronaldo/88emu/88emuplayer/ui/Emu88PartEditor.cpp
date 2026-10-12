#include "88emuplayer/ui/Emu88PartEditor.h"

#include "88emuplayer/Emu88Processor.h"
#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"

#include "88lib/gs/gs.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/juceRmlComponentConfig.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlMenu.h"

#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/ScrollTypes.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"
#include "RmlUi/Core/StringUtilities.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sstream>

namespace emu88Player
{
	using namespace editor;
	namespace gs = emu88Lib::gs;

	namespace
	{
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
		startTimerHz(30);
	}

	PartEditorWindow::~PartEditorWindow()
	{
		stopTimer();
		// A solo lasts as long as the window: the parts it muted play again
		m_soloed = 0;
		applySolo();
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
		m_pending.clear();
		m_listKey.clear();
		m_soloed = m_soloMuted = 0;
		juceRmlUi::RmlInterfaces::ScopedAccess access(*m_rml);
		build();
		m_rml->enqueueUpdate();
	}

	void PartEditorWindow::build()
	{
		m_model = m_processor.deviceModel();
		const bool supported = gs::isPartEditorModel(m_model);
		m_groupCount = supported ? std::max<uint8_t>(1, emu88Lib::getDeviceProfile(m_model).groupCount) : 0;
		m_group = std::min<uint8_t>(m_group, std::max<uint8_t>(1, m_groupCount) - 1);
		m_setup = m_processor.partState().setup(m_revision);

		m_catalog = m_processor.toneCatalog();
		m_catalogError = m_processor.toneCatalogError();

		setName(juce::String(emu88Lib::getDeviceProfile(m_model).displayName) + " Part Editor");

		buildGroupAndPartTabs();
		buildMixer();
		selectPage(supported ? m_page : g_mixerPage);

		if(!supported)
			showStatus("The part editor works with the Sound Canvas boards, from the SC-55 to the SC-8850.", true);
		else if(!m_catalog)
			showStatus("Instrument names unavailable: " + m_catalogError, true);
		else
			showStatus({});
	}

	void PartEditorWindow::wireActions()
	{
		juceRmlUi::EventListener::AddClick(m_root->GetElementById("peTabMixer"), [this] { selectPage(g_mixerPage); });

		juceRmlUi::EventListener::AddClick(m_root->GetElementById("peSendPart"), [this]
		{
			if(isActionEnabled(m_root->GetElementById("peSendPart")))
				sendParts(selectedPart(), 1, false);
		});
		juceRmlUi::EventListener::AddClick(m_root->GetElementById("peReceivePart"), [this]
		{
			if(isActionEnabled(m_root->GetElementById("peReceivePart")))
				receiveParts(selectedPart(), 1, false);
		});
		juceRmlUi::EventListener::AddClick(m_root->GetElementById("peSendAll"), [this]
		{
			if(isActionEnabled(m_root->GetElementById("peSendAll")))
				sendParts(0, m_setup.parts.size(), true);
		});
		juceRmlUi::EventListener::AddClick(m_root->GetElementById("peReceiveAll"), [this]
		{
			if(isActionEnabled(m_root->GetElementById("peReceiveAll")))
				receiveParts(0, m_setup.parts.size(), true);
		});

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
		juceRmlUi::EventListener::Add(m_root, Rml::EventId::Mouseup, [this](Rml::Event&) { m_dragging = nullptr; });

		juceRmlUi::EventListener::Add(m_root, Rml::EventId::Keydown, [this](Rml::Event& _event)
		{
			if(juceRmlUi::helper::getKeyIdentifier(_event) == Rml::Input::KI_ESCAPE)
			{
				_event.StopPropagation();
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

		std::ostringstream rml;
		if(m_groupCount > 1)
		{
			for(uint8_t g = 0; g < m_groupCount; ++g)
				rml << "<button id=\"peGroup" << int(g) << "\" class=\"peGroup\" title=\"Part group " << groupLetter(g)
				    << "\">" << groupLetter(g) << "</button>";
		}
		groups->SetInnerRML(rml.str());

		rml.str({});
		if(m_groupCount)
		{
			for(int p = 0; p < gs::g_partsPerGroup; ++p)
				rml << "<button id=\"pePartTab" << p << "\" class=\"pePartTab\">" << (p + 1) << "</button>";
		}
		parts->SetInnerRML(rml.str());

		for(uint8_t g = 0; g < m_groupCount && m_groupCount > 1; ++g)
		{
			auto* tab = groups->GetElementById("peGroup" + std::to_string(g));
			m_groupTabs.push_back(tab);
			juceRmlUi::EventListener::AddClick(tab, [this, g] { selectGroup(g); });
		}
		for(int p = 0; p < gs::g_partsPerGroup && m_groupCount; ++p)
		{
			auto* tab = parts->GetElementById("pePartTab" + std::to_string(p));
			m_partTabs.push_back(tab);
			juceRmlUi::EventListener::AddClick(tab, [this, p] { selectPage(p); });
			juceRmlUi::EventListener::Add(tab, Rml::EventId::Mousedown, [this, p](Rml::Event& _event)
			{
				if(!juceRmlUi::helper::isContextMenu(_event))
					return;
				_event.StopPropagation();
				openPartMenu(p, _event);
			});
		}
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

		const auto partLabel = [this](const size_t _i)
		{
			const auto part = static_cast<int>(_i % gs::g_partsPerGroup) + 1;
			return m_groupCount > 1 ? groupLetter(static_cast<uint8_t>(_i / gs::g_partsPerGroup)) + number(part, 2)
			                        : std::to_string(part);
		};

		std::ostringstream rml;
		for(size_t i = 0; i < m_setup.parts.size(); ++i)
		{
			if(m_groupCount > 1 && i % gs::g_partsPerGroup == 0)
				rml << "<div class=\"peMixGroupHeading\">Part group "
				    << groupLetter(static_cast<uint8_t>(i / gs::g_partsPerGroup)) << "</div>";
			rml << "<div id=\"peMix" << i << "\" class=\"peMixRow\">"
			    << "<div class=\"peMixSoloMute\"><button id=\"peSolo" << i << "\" class=\"peMixSolo\" title=\"Solo\">S</button>"
			    << "<button id=\"peMute" << i << "\" class=\"peMixMute\" title=\"Mute\">M</button></div>"
			    << "<div id=\"peMixName" << i << "\" class=\"peMixName\" title=\"Edit this part\">"
			    << "<div class=\"peMixPart\">" << partLabel(i) << "</div>"
			    << "<div id=\"peMixTone" << i << "\" class=\"peMixTone\"/></div>"
			    << "<div class=\"peMixVolume\"><input id=\"peVol" << i
			    << "\" class=\"peSlider\" type=\"range\" min=\"0\" max=\"127\" step=\"1\" value=\"100\"/>"
			    << "<div id=\"peVolValue" << i << "\" class=\"peMixValue\"/></div>"
			    << "<div class=\"peMixPan\"><input id=\"pePan" << i
			    << "\" class=\"peSlider\" type=\"range\" min=\"0\" max=\"127\" step=\"1\" value=\"64\"/>"
			    << "<div id=\"pePanValue" << i << "\" class=\"peMixValue\"/></div></div>";
		}
		rows->SetInnerRML(rml.str());

		auto* document = rows->GetOwnerDocument();
		for(size_t i = 0; i < m_setup.parts.size(); ++i)
		{
			const auto id = std::to_string(i);
			MixerRow row;
			row.row = document->GetElementById("peMix" + id);
			row.solo = document->GetElementById("peSolo" + id);
			row.mute = document->GetElementById("peMute" + id);
			row.tone = document->GetElementById("peMixTone" + id);
			row.volume = dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("peVol" + id));
			row.pan = dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("pePan" + id));
			row.volumeValue = document->GetElementById("peVolValue" + id);
			row.panValue = document->GetElementById("pePanValue" + id);

			addClick(row.solo, [this, i] { toggleSolo(i); });
			addClick(row.mute, [this, i] { toggleMute(i); });
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
					if(m_updatingUi || i >= m_setup.parts.size())
						return;
					const auto value = static_cast<uint8_t>(std::clamp(std::atoi(slider->GetValue().c_str()), 0, 127));
					auto& p = m_setup.parts[i];
					auto& current = isVolume ? p.level : p.pan;
					if(current == value)
						return;
					// Shown at once; the board's echo through the part state confirms it a moment later
					current = value;
					const auto part = static_cast<uint8_t>(i % gs::g_partsPerGroup);
					setPartValue(i, gs::partAddress(part, isVolume ? gs::PartParam::Level : gs::PartParam::Pan), {value});
					const auto& r = m_mixerRows[i];
					setText(isVolume ? r.volumeValue : r.panValue, isVolume ? std::to_string(value) : panText(value));
				});
			}
			m_mixerRows.push_back(row);
		}
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

	void PartEditorWindow::openPartMenu(const int _page, Rml::Event& _event)
	{
		const auto index = static_cast<size_t>(m_group) * gs::g_partsPerGroup + static_cast<size_t>(_page);
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

	void PartEditorWindow::setPartType(const size_t _part, const uint8_t _useForRhythm)
	{
		// Runs from the menu's own window: the board gets it now, the timer shows what comes back
		if(_part >= m_setup.parts.size() || m_setup.parts[_part].useForRhythm == _useForRhythm)
			return;
		const auto part = static_cast<uint8_t>(_part % gs::g_partsPerGroup);
		setPartValue(_part, gs::partAddress(part, gs::PartParam::UseForRhythm), {_useForRhythm});
		flushPending();
	}

	void PartEditorWindow::selectGroup(const uint8_t _group)
	{
		if(_group >= m_groupCount)
			return;
		m_group = _group;
		m_listKey.clear();
		// The groups are part pages too: from the mixer one goes back to the part it last showed
		if(m_page == g_mixerPage)
		{
			selectPage(m_lastPartPage);
			return;
		}
		refresh();
	}

	void PartEditorWindow::selectPart(const size_t _part)
	{
		if(_part >= m_setup.parts.size())
			return;
		m_group = static_cast<uint8_t>(_part / gs::g_partsPerGroup);
		selectPage(static_cast<int>(_part % gs::g_partsPerGroup));
	}

	void PartEditorWindow::selectPage(const int _page)
	{
		m_page = m_groupCount ? std::clamp(_page, g_mixerPage, gs::g_partsPerGroup - 1) : g_mixerPage;
		if(m_page != g_mixerPage)
			m_lastPartPage = m_page;
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
		// One selection across the mixer, the groups and the parts, as radio buttons
		for(size_t g = 0; g < m_groupTabs.size(); ++g)
		{
			if(m_groupTabs[g])
				m_groupTabs[g]->SetPseudoClass("checked", m_page != g_mixerPage && g == m_group);
		}
		for(size_t p = 0; p < m_partTabs.size(); ++p)
		{
			auto* tab = m_partTabs[p];
			if(!tab)
				continue;
			tab->SetPseudoClass("checked", static_cast<int>(p) == m_page);
			const auto index = static_cast<size_t>(m_group) * gs::g_partsPerGroup + p;
			const bool drum = index < m_setup.parts.size() && m_setup.parts[index].isDrumPart();
			tab->SetClass("drum", drum);
			const auto title = toneName(index);
			if(tab->GetAttribute<Rml::String>("title", "") != title)
				tab->SetAttribute("title", title);
		}
	}

	void PartEditorWindow::refreshMixer()
	{
		m_updatingUi = true;
		for(size_t i = 0; i < m_mixerRows.size() && i < m_setup.parts.size(); ++i)
		{
			const auto& p = m_setup.parts[i];
			const auto& row = m_mixerRows[i];
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
			if(row.volume && row.volume != m_dragging)
			{
				const auto value = std::to_string(p.level);
				if(row.volume->GetValue() != value)
					row.volume->SetValue(value);
				setText(row.volumeValue, value);
			}
			if(row.pan && row.pan != m_dragging)
			{
				const auto value = std::to_string(p.pan);
				if(row.pan->GetValue() != value)
					row.pan->SetValue(value);
				setText(row.panValue, panText(p.pan));
			}
		}
		m_updatingUi = false;
	}

	void PartEditorWindow::refreshPart()
	{
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

		// The maps, for a board with more than one
		auto* maps = m_root->GetElementById("peMaps");
		std::ostringstream rml;
		const bool showMaps = m_catalog && gs::hasToneMaps(m_model) && m_catalog->maps.size() > 1;
		if(showMaps)
		{
			for(size_t m = 0; m < m_catalog->maps.size(); ++m)
				rml << "<button id=\"peMap" << m << "\" class=\"peMap\">" << encode(m_catalog->maps[m].name) << "</button>";
		}
		maps->SetInnerRML(rml.str());
		if(showMaps)
		{
			for(size_t m = 0; m < m_catalog->maps.size(); ++m)
			{
				auto* button = maps->GetElementById("peMap" + std::to_string(m));
				const auto number = m_catalog->maps[m].number;
				if(!button)
					continue;
				button->SetPseudoClass("checked", map == &m_catalog->maps[m]);
				addClick(button, [this, index, number] { selectToneMap(index, number); });
			}
		}

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

		rml.str({});
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
			addClick(row.element, [this, index, program, bank, drum] { selectTone(index, drum ? 0 : bank, program); });
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

		// A selection that came from elsewhere is brought into view, by as little as it takes
		if(selected)
			selected->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
				Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest));
	}

	void PartEditorWindow::refreshActions()
	{
		const bool supported = m_groupCount > 0;
		const bool idle = supported && !m_receiving;
		setActionEnabled(m_root->GetElementById("peSendPart"), idle && m_page != g_mixerPage);
		setActionEnabled(m_root->GetElementById("peReceivePart"), idle && m_page != g_mixerPage);
		setActionEnabled(m_root->GetElementById("peSendAll"), idle);
		setActionEnabled(m_root->GetElementById("peReceiveAll"), idle);
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

	void PartEditorWindow::setPartValue(const size_t _part, const uint32_t _address, const std::vector<uint8_t>& _data)
	{
		const auto port = static_cast<uint8_t>(_part / gs::g_partsPerGroup);
		m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(), [&](const PendingValue& _v)
		{
			return _v.port == port && _v.address == _address;
		}), m_pending.end());
		m_pending.push_back({port, _address, _data});
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
			const auto message = gs::dt1(value.address, value.data);
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

	void PartEditorWindow::sendParts(const size_t _first, const size_t _count, const bool _withMasterVolume)
	{
		flushPending();
		const bool maps = gs::hasToneMaps(m_model);
		std::vector<synthLib::SMidiEvent> events;
		const auto add = [&](const uint8_t _port, const gs::Message& _message)
		{
			auto& event = events.emplace_back(synthLib::MidiEventSource::Host);
			event.sysex.assign(_message.begin(), _message.end());
			event.port = _port;
		};

		if(_withMasterVolume)
			add(0, gs::dt1(gs::g_masterVolume, m_setup.masterVolume));

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

			// The SC-8850 applies the map with a program change only, which reaches every part on the channel; the
			// parts that have their channel to themselves get one
			if(isSc8850(m_model))
			{
				const auto first = static_cast<size_t>(port) * gs::g_partsPerGroup;
				const auto sharers = std::count_if(m_setup.parts.begin() + static_cast<ptrdiff_t>(first),
					m_setup.parts.begin() + static_cast<ptrdiff_t>(first + gs::g_partsPerGroup),
					[&](const gs::Part& _other) { return _other.rxChannel == p.rxChannel; });
				if(sharers == 1)
					programChange(i, events);
			}
		}
		m_processor.sendMidiEvents(events);
		showStatus(_count == 1 ? "Sent the part to the device." : "Sent every part to the device.");
	}

	void PartEditorWindow::receiveParts(const size_t _first, const size_t _count, const bool _withMasterVolume)
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

		if(_withMasterVolume)
			add(0, gs::g_masterVolume, 1);

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

		flushPending();

		bool changed = false;
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
						" values; the device did not answer the rest.", true);
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

		if(changed)
			m_rml->enqueueUpdate();
	}
}
