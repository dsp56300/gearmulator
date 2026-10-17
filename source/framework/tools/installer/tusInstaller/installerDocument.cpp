#include "installerDocument.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <set>

#include "changelogPage.h"
#include "detailPage.h"
#include "installerDataProvider.h"
#include "pluginEntryController.h"
#include "preferencesPage.h"

#include "pluginManagerLib/config.h"
#include "pluginManagerLib/httpClient.h"
#include "pluginManagerLib/log.h"
#include "pluginManagerLib/selfUpdate.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"
#include "juceRmlUi/rmlMenu.h"
#include "juceRmlUi/rmlTooltips.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"

#include <JuceHeader.h>

namespace pluginManager
{
	namespace
	{
		// The heading of the group the products with an update are put into, see rebuild().
		constexpr const char* g_updatesGroup = "Updates available";
	}

	namespace
	{
		// RmlUi event handlers must not call back into InstallerDocument, whose methods take RmlUi access
		// themselves. What a handler reports is posted instead, and the app answers from there.
		//
		// ponytail: the posted call holds the callback, not the document. That is safe because the document
		// only goes away at shutdown, when JUCE drops queued messages; guard it with an instance set if a
		// document can ever be destroyed while the app keeps running.
		void post(std::function<void()> _call)
		{
			juce::MessageManager::callAsync([call = std::move(_call)]
			{
				if (call)
					call();
			});
		}
	}

	InstallerDocument::InstallerDocument(InstallerDataProvider& _dataProvider)
		: m_dataProvider(_dataProvider)
	{
		// Before anything builds a row, because that is what reads it.
		loadSelection();

		m_rmlInterfaces = std::make_unique<juceRmlUi::RmlInterfaces>(m_dataProvider);

		juceRmlUi::RmlComponentConfig config;
		config.includeDefaultTemplates = false;

		m_rmlComponent = std::make_unique<juceRmlUi::RmlComponent>(*m_rmlInterfaces, m_dataProvider, "installer.rml", 1.0f,
			nullptr, [](juceRmlUi::RmlComponent&, Rml::Context&) {}, config);
	}

	InstallerDocument::~InstallerDocument()
	{
		// A thumbnail download started before this went away still has an answer to deliver.
		*m_alive = false;

		// The rows hold what is ticked and they are about to go, so it is taken off them - and
		// written down - while they are still here.
		rememberSelection();
	}

	void InstallerDocument::setHandlers(std::function<void()> _onTeamLogin, std::function<void()> _onDonatorLogin,
	                                    std::function<void()> _onRefresh)
	{
		m_onTeamLogin = std::move(_onTeamLogin);
		m_onDonatorLogin = std::move(_onDonatorLogin);
		m_onRefresh = std::move(_onRefresh);
	}

	Rml::ElementDocument* InstallerDocument::getDocument() const
	{
		return m_rmlComponent ? m_rmlComponent->getDocument() : nullptr;
	}

	void InstallerDocument::withDocument(const std::function<void(Rml::ElementDocument&)>& _func)
	{
		auto* doc = getDocument();

		if (!doc)
			return;

		{
			juceRmlUi::RmlInterfaces::ScopedAccess sa(*m_rmlComponent);

			bindDocument(*doc);
			_func(*doc);
		}

		m_rmlComponent->enqueueUpdate();
	}

	// The component only loads its document once it has a size, which is after this object is constructed.
	// So the controls the document itself provides are bound the first time it is touched.
	void InstallerDocument::bindDocument(Rml::ElementDocument& _doc)
	{
		if (m_login)
			return;

		m_login = std::make_unique<LoginDialog>(_doc);
		m_detail = std::make_unique<DetailPage>(_doc, m_dataProvider, *m_rmlComponent);
		m_changelog = std::make_unique<ChangelogPage>(_doc, *m_rmlComponent);
		m_tooltips = std::make_unique<juceRmlUi::Tooltips>(*m_rmlComponent, _doc);
		m_pluginList = _doc.GetElementById("plugin-list");
		m_formatToggles = _doc.GetElementById("format-toggles");
		m_selectAll = _doc.GetElementById("select-all");

		// Right-click, which RmlUi has no event of its own for.
		if (m_pluginList)
		{
			juceRmlUi::EventListener::Add(m_pluginList, Rml::EventId::Mousedown, [this](Rml::Event& _event)
			{
				if (juceRmlUi::helper::isContextMenu(_event))
					openListMenu(_event);
			});
		}

		if (m_selectAll)
		{
			juceRmlUi::EventListener::Add(m_selectAll, Rml::EventId::Click, [this](Rml::Event&)
			{
				// What it says is what it does, so the state it is in decides which way this goes.
				const auto select = !everythingSelected();

				for (auto& entry : m_entries)
					entry->setAllFormatsSelected(select);

				updateFormatChips();
				updateBulkButtons();

				m_rmlComponent->enqueueUpdate();
			});
		}
		if (auto* title = _doc.GetElementById("app-title"))
			title->SetInnerRML(g_appTitle);

		if (auto* version = _doc.GetElementById("app-version"))
			version->SetInnerRML(g_appVersion);

		if (auto* website = _doc.GetElementById("website-link"))
		{
			juceRmlUi::EventListener::Add(website, Rml::EventId::Click, [](Rml::Event&)
			{
				juce::URL(g_websiteUrl).launchInDefaultBrowser();
			});
		}

		if (auto* back = m_detail->getBackButton())
		{
			juceRmlUi::EventListener::Add(back, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this]
				{
					// The page being left installs and removes things as well, and the rows behind it have
					// been showing what was true when the list was built. Asked every time rather than only
					// when something happened: a flag that says whether it did is one more thing to get
					// wrong, and this costs a look at the disk.
					refreshInstalled();
					showPage(Page::List);
				});
			});
		}

		if (auto* changelog = _doc.GetElementById("detail-changelog"))
		{
			juceRmlUi::EventListener::Add(changelog, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this] { showChangelog(); });
			});
		}

		// Back to the product it was opened from, which is where it was asked for.
		if (auto* back = m_changelog->getBackButton())
		{
			juceRmlUi::EventListener::Add(back, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this] { showPage(Page::Detail); });
			});
		}

		m_selfUpdate = _doc.GetElementById("self-update");

		if (m_selfUpdate)
		{
			juceRmlUi::EventListener::Add(m_selfUpdate, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this] { startSelfUpdate(); });
			});
		}

		m_refreshButton = _doc.GetElementById("refresh-button");

		if (m_refreshButton)
		{
			juceRmlUi::EventListener::Add(m_refreshButton, Rml::EventId::Click, [this](Rml::Event&)
			{
				// Not while one is already on its way. A catalog is a request per release now, and
				// pressing this twice would put two of those on the wire for one answer.
				if (m_busy)
					return;

				post(m_onRefresh);
			});
		}

		if (auto* preferences = _doc.GetElementById("preferences-button"))
		{
			juceRmlUi::EventListener::Add(preferences, Rml::EventId::Click, [this](Rml::Event&)
			{
				// A second press while it is open closes it, the same way Back does.
				post([this]
				{
					if (m_page == Page::Preferences)
						closePreferences();
					else
						showPage(Page::Preferences);
				});
			});
		}

		m_installSelected = _doc.GetElementById("install-selected");
		m_upgradeAll = _doc.GetElementById("upgrade-all");

		if (m_installSelected)
		{
			juceRmlUi::EventListener::Add(m_installSelected, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this] { startBulk(PluginEntryController::Bulk::Install); });
			});
		}

		if (m_upgradeAll)
		{
			juceRmlUi::EventListener::Add(m_upgradeAll, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this] { startBulk(PluginEntryController::Bulk::Upgrade); });
			});
		}

		if (auto* donatorLogin = _doc.GetElementById("donator-login"))
			juceRmlUi::EventListener::Add(donatorLogin, Rml::EventId::Click, [this](Rml::Event&) { post(m_onDonatorLogin); });

		m_teamLogin = _doc.GetElementById("team-login");
		m_teamLoginSection = _doc.GetElementById("team-login-section");

		if (!m_teamLogin)
			return;

		updateTeamLogin();

		juceRmlUi::EventListener::Add(m_teamLogin, Rml::EventId::Click, [this](Rml::Event&)
		{
			post(m_onTeamLogin);
		});
	}

	// Every format any product is offered in here, in the order the rows lay their columns out in.
	// Taken from the catalog rather than from the platform, so it is exactly what can be installed.
	std::vector<std::string> InstallerDocument::collectFormats(const Catalog& _catalog)
	{
		std::vector<std::string> formats;

		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		for (const auto& product : _catalog.getProducts())
		{
			// Its own build carries no format segment, which reads as Standalone - a column heading
			// for a format that no row below it is offered in.
			if (isAppProduct(product.product))
				continue;

			for (const auto& format : _catalog.getFormats(product.product, os, arch))
			{
				if (std::find(formats.begin(), formats.end(), format) == formats.end())
					formats.push_back(format);
			}
		}

		return formats;
	}

	void InstallerDocument::buildFormatToggles(Rml::ElementDocument& _doc, const std::vector<std::string>& _formats)
	{
		if (!m_formatToggles)
			return;

		// Only the chips: the label, the spacer and the two buttons beside them belong to the
		// document rather than to the catalog.
		for (int i = m_formatToggles->GetNumChildren() - 1; i >= 0; --i)
		{
			auto* child = m_formatToggles->GetChild(i);

			if (child->IsClassSet("format-label"))
				m_formatToggles->RemoveChild(child);
		}

		// Before the spacer, so the chips stay on the left and the two buttons on the right.
		auto* spacer = m_formatToggles->QuerySelector(".header-spacer");

		for (const auto& format : _formats)
		{
			auto element = _doc.CreateElement("span");

			auto* chip = spacer
				? m_formatToggles->InsertBefore(std::move(element), spacer)
				: m_formatToggles->AppendChild(std::move(element));

			if (!chip)
				continue;

			// Ticked unless every row offering this format has it unticked. The chip sets rather than
			// filters, so the honest thing for it to show is whether the format is still wanted
			// anywhere - and asking the rows is what carries it through a rebuild, since the rows are
			// built before this runs and carry what they were given back.
			chip->SetClassNames(isFormatSelectedAnywhere(format) ? "format-label checked" : "format-label");
			chip->SetInnerRML(format);

			juceRmlUi::EventListener::Add(chip, Rml::EventId::Click, [this, chip, format](Rml::Event&)
			{
				const auto selected = !chip->IsClassSet("checked");

				chip->SetClass("checked", selected);

				// Not posted: this only touches the rows, which is what the handler is allowed to do.
				setFormatSelectedEverywhere(format, selected);
			});
		}
	}

	// Updated for the products on show and left alone for the rest: a product can be out of the
	// catalog for a while - everything internal is, between a team logout and the next login - and
	// coming back must not mean coming back unticked.
	//
	// A file from before EMU-248 says what was unticked instead, and is not read: what it meant was
	// "everything else", and that is the default this no longer has.
	void InstallerDocument::loadSelection()
	{
		m_selection.clear();

		const auto xml = juce::XmlDocument::parse(juce::File(
			juce::String(PlatformUtils::getSettingsPath("selection.xml"))));

		if (!xml)
			return;

		for (auto* e : xml->getChildIterator())
		{
			const auto id = e->getStringAttribute("id").toStdString();

			if (!e->hasTagName("product") || id.empty())
				continue;

			auto& selection = m_selection[id];

			selection.fx = e->getIntAttribute("fx", 0) != 0;

			for (const auto& format : juce::StringArray::fromTokens(
				e->getStringAttribute("selected"), ",", {}))
			{
				if (format.isNotEmpty())
					selection.selectedFormats.insert(format.toStdString());
			}
		}
	}

	void InstallerDocument::saveSelection() const
	{
		juce::XmlElement xml("selection");

		for (const auto& [product, selection] : m_selection)
		{
			// Nothing ticked and no effect build asked for is what a row looks like untouched. Writing
			// those out would put every product on the server into the file to say nothing.
			if (selection.selectedFormats.empty() && !selection.fx)
				continue;

			auto* e = xml.createNewChildElement("product");
			e->setAttribute("id", juce::String(product));

			if (selection.fx)
				e->setAttribute("fx", 1);

			juce::StringArray formats;

			for (const auto& format : selection.selectedFormats)
				formats.add(juce::String(format));

			if (!formats.isEmpty())
				e->setAttribute("selected", formats.joinIntoString(","));
		}

		const auto file = juce::File(juce::String(PlatformUtils::getSettingsPath("selection.xml")));

		file.getParentDirectory().createDirectory();
		xml.writeTo(file);
	}

	void InstallerDocument::rememberSelection()
	{
		for (const auto& entry : m_entries)
		{
			const auto selected = entry->getSelectedFormats();

			auto& selection = m_selection[entry->getProductId()];

			selection.selectedFormats.clear();
			selection.selectedFormats.insert(selected.begin(), selected.end());
			selection.fx = entry->isFxSelected();
		}

		saveSelection();
	}

	void InstallerDocument::applySelection(PluginEntryController& _entry) const
	{
		const auto it = m_selection.find(_entry.getProductId());

		if (it == m_selection.end())
			return;

		for (const auto& format : it->second.selectedFormats)
			_entry.setFormatSelected(format, true);

		_entry.setFxSelected(it->second.fx);
	}

	// Empty rather than vacuously true when there is nothing on offer: a list with no rows must not
	// say "Deselect all".
	bool InstallerDocument::everythingSelected() const
	{
		if (m_entries.empty())
			return false;

		for (const auto& entry : m_entries)
		{
			if (!entry->areAllFormatsSelected())
				return false;
		}

		return true;
	}

	void InstallerDocument::updateSelectAll() const
	{
		if (m_selectAll)
			m_selectAll->SetInnerRML(everythingSelected() ? "Deselect all" : "Select all");
	}

	void InstallerDocument::refreshInstalled()
	{
		withDocument([&](Rml::ElementDocument&)
		{
			for (const auto& entry : m_entries)
				entry->refreshInstalled();
		});
	}

	void InstallerDocument::updateFormatChips() const
	{
		if (!m_formatToggles)
			return;

		for (int i = 0; i < m_formatToggles->GetNumChildren(); ++i)
		{
			auto* chip = m_formatToggles->GetChild(i);

			if (!chip->IsClassSet("format-label"))
				continue;

			chip->SetClass("checked", isFormatSelectedAnywhere(chip->GetInnerRML()));
		}
	}

	bool InstallerDocument::isFormatSelectedAnywhere(const std::string& _format) const
	{
		for (const auto& entry : m_entries)
		{
			// Only rows that have the format count. A row without it says it is not holding it back,
			// and taking that for a tick left every chip lit for ever: one Standalone-only row - the
			// bridge server, a test console, 88emuPlayer - vouches for VST3, VST2, CLAP and LV2 while
			// offering none of them, and the plugin rows vouch the same way for Standalone.
			if (entry->offersFormat(_format) && entry->isFormatSelected(_format))
				return true;
		}

		return false;
	}

	void InstallerDocument::setFormatSelectedEverywhere(const std::string& _format, const bool _selected)
	{
		for (auto& entry : m_entries)
			entry->setFormatSelected(_format, _selected);

		updateBulkButtons();

		m_rmlComponent->enqueueUpdate();
	}

	// Each button says how much it would do, and is dim when that is nothing - the same as the
	// buttons in the All column of a row, which is where these numbers come from.
	void InstallerDocument::updateBulkButtons()
	{
		// Before the run check below: what the button offers is true whether or not one is running.
		updateSelectAll();

		// A run in progress writes its own text - see bulkNext - and is not counted over.
		if (!m_bulkQueue.empty())
			return;

		size_t toInstall = 0;
		size_t toUpgrade = 0;

		for (const auto& entry : m_entries)
		{
			toInstall += entry->countFor(PluginEntryController::Bulk::Install);
			toUpgrade += entry->countFor(PluginEntryController::Bulk::Upgrade);
		}

		const auto label = [](Rml::Element* _button, const std::string& _text, const size_t _count)
		{
			if (!_button)
				return;

			_button->SetInnerRML(_count ? _text + " (" + std::to_string(_count) + ")" : _text);
			_button->SetClass("ready", _count > 0);
		};

		label(m_installSelected, "Install selected", toInstall);
		label(m_upgradeAll, "Upgrade all", toUpgrade);
	}

	void InstallerDocument::startBulk(const PluginEntryController::Bulk _what)
	{
		if (!m_bulkQueue.empty())
			return;

		m_bulkWhat = _what;
		m_bulkElevated = std::make_shared<ElevatedBatch>();

		withDocument([&](Rml::ElementDocument&)
		{
			for (auto& entry : m_entries)
			{
				if (entry->countFor(_what))
					m_bulkQueue.push_back(entry.get());
			}
		});

		bulkNext();
	}

	// One row, then the next. The row answers when it is through, whether it installed anything or
	// stopped on a failure, so the walk cannot get stuck on one.
	void InstallerDocument::finishBulkElevated()
	{
		if (!m_bulkElevated || m_bulkElevated->empty() || !m_pluginManager)
		{
			m_bulkElevated.reset();
			return;
		}

		setStatus("Waiting for administrator rights...");

		std::string error;
		const auto done = m_pluginManager->finishPending(*m_bulkElevated, error);

		m_bulkElevated.reset();

		withDocument([&](Rml::ElementDocument&)
		{
			// Read off the disk rather than assumed from what was asked for, which is the only thing
			// still true after a prompt was declined halfway through.
			for (auto& entry : m_entries)
				entry->refreshAll();
		});

		setStatus(done ? std::string() : "Failed: " + error);
	}

	void InstallerDocument::bulkNext()
	{
		if (m_bulkQueue.empty())
		{
			finishBulkElevated();

			withDocument([&](Rml::ElementDocument&) { updateBulkButtons(); });

			// What was upgraded goes back to its category, out of "Updates available" (EMU-250). Once
			// the run is over rather than row by row, so nothing moves while it is being watched - and
			// posted, since the rebuild takes away the rows whose callbacks got this far.
			if (m_bulkWhat == PluginEntryController::Bulk::Upgrade)
			{
				post([this]
				{
					if (m_pluginManager)
						rebuild(*m_pluginManager);
				});
			}
			return;
		}

		auto* entry = m_bulkQueue.front();

		withDocument([&](Rml::ElementDocument&)
		{
			const auto left = m_bulkQueue.size();

			if (auto* button = m_bulkWhat == PluginEntryController::Bulk::Install ? m_installSelected : m_upgradeAll)
				button->SetInnerRML(std::to_string(left) + " to go...");

			entry->startBulk(m_bulkWhat, [this]
			{
				// Out of the install callback before the next row is started, since starting one
				// goes through the document again.
				post([this]
				{
					if (!m_bulkQueue.empty())
						m_bulkQueue.erase(m_bulkQueue.begin());

					bulkNext();
				});
			}, m_bulkElevated);
		});
	}

	void InstallerDocument::createPreferencesPage(Rml::ElementDocument& _doc, PluginManager& _pluginManager)
	{
		if (m_preferences)
			return;

		m_preferences = std::make_unique<PreferencesPage>(_doc, _pluginManager, *m_rmlComponent);

		// Both belong to the app: one opens a prompt, the other reads the catalog again.
		m_preferences->setHandlers([this] { post(m_onDonatorLogin); }, [this] { post(m_onRefresh); });

		if (auto* back = m_preferences->getBackButton())
		{
			juceRmlUi::EventListener::Add(back, Rml::EventId::Click, [this](Rml::Event&)
			{
				post([this] { closePreferences(); });
			});
		}
	}

	void InstallerDocument::closePreferences()
	{
		if (!m_preferences)
			return;

		m_preferences->save();

		// A location that changed is what decides whether a plugin counts as installed, so the list is
		// built again on the way back.
		if (m_pluginManager)
			rebuild(*m_pluginManager);

		showPage(Page::List);
	}

	// Only one of the three is visible at a time. The list has no page class of its own because it is
	// what the document starts on.
	void InstallerDocument::showPage(const Page _page)
	{
		m_page = _page;

		withDocument([&](Rml::ElementDocument&)
		{
			if (m_pluginList)
				m_pluginList->SetClass("hidden", _page != Page::List);

			if (m_formatToggles)
				m_formatToggles->SetClass("hidden", _page != Page::List);

			if (m_detail && m_detail->getPage())
				m_detail->getPage()->SetClass("open", _page == Page::Detail);

			if (m_changelog && m_changelog->getPage())
				m_changelog->getPage()->SetClass("open", _page == Page::Changelog);

			if (m_preferences && m_preferences->getPage())
			{
				if (_page == Page::Preferences)
					m_preferences->refresh();

				m_preferences->getPage()->SetClass("open", _page == Page::Preferences);
			}
		});
	}

	void InstallerDocument::showDetail(const Product& _product)
	{
		if (!m_pluginManager)
			return;

		withDocument([&](Rml::ElementDocument&)
		{
			if (m_detail)
				m_detail->show(_product, *m_pluginManager);
		});

		showPage(Page::Detail);
	}

	void InstallerDocument::showChangelog()
	{
		if (!m_pluginManager || !m_detail || !m_changelog)
			return;

		const auto* product = m_pluginManager->getCatalog().findProduct(m_detail->getProductId());

		if (!product)
			return;

		withDocument([&](Rml::ElementDocument&)
		{
			m_changelog->show(*product, *m_pluginManager);
		});

		showPage(Page::Changelog);
	}

	void InstallerDocument::rebuild(PluginManager& _pluginManager)
	{
		m_pluginManager = &_pluginManager;

		const auto& catalog = _pluginManager.getCatalog();

		withDocument([&](Rml::ElementDocument& _doc)
		{
			auto* container = _doc.GetElementById("plugin-list");

			if (!container)
				return;

			// Taken off the rows while they are still here, because what the user ticked lives in them
			// and nowhere else.
			rememberSelection();

			// The controllers hold pointers into the rows, so they go before the elements do - and
			// a run over them cannot outlive the rows it was walking.
			m_bulkQueue.clear();
			m_entries.clear();

			// Every row but the template. The template is the first child, so the loop must not stop at it.
			for (int i = container->GetNumChildren() - 1; i >= 0; --i)
			{
				auto* child = container->GetChild(i);

				if (child->GetId() != "plugin-entry-template")
					container->RemoveChild(child);
			}

			size_t shown = 0;

			m_hiddenCount = 0;

			// The same for every row, so that a format sits in the same column all the way down.
			const auto formats = collectFormats(catalog);

			std::vector<const Product*> rows;

			for (const auto& product : catalog.getProducts())
			{
				// A product with nothing installable here would be a row with no buttons.
				if (catalog.getFormats(product.product, PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch()).empty())
					continue;

				// This program is in the list so that its own builds are fetched, but it is not a row: it
				// does not go where plugins go, and the button that replaces it is in the header.
				if (isAppProduct(product.product))
					continue;

				// Hidden on this machine. Counted rather than passed over in silence, so that a list missing
				// something says so and the right-click that brings it back can be found.
				if (_pluginManager.getInstallPaths().isHidden(product.product))
				{
					++m_hiddenCount;
					continue;
				}

				rows.push_back(&product);
			}

			// What has an update comes first, under a heading of its own (EMU-250): with the products
			// that have one spread over the whole list, "Upgrade all" worked through rows that were
			// mostly out of sight. A product goes back to its category once the list is built again,
			// which a run of upgrades does when it ends.
			std::set<const Product*> updatable;

			for (const auto* product : rows)
			{
				if (_pluginManager.hasUpdate(product->product))
					updatable.insert(product);
			}

			const auto categoryOf = [&updatable](const Product* _product) -> const std::string&
			{
				static const std::string updates = g_updatesGroup;
				return updatable.count(_product) ? updates : _product->category;
			};

			// Grouped under the category the product lists give them, the groups in the order they
			// first come up there and the rows within one by manufacturer and year, see sortsBefore().
			// Products without a category go last, and are only put under a heading of their own when
			// there are groups above them: otherwise they would read as part of the last one.
			std::vector<std::string> groups;

			if (!updatable.empty())
				groups.push_back(g_updatesGroup);

			for (const auto* product : rows)
			{
				const auto& category = categoryOf(product);

				if (!category.empty() && std::find(groups.begin(), groups.end(), category) == groups.end())
					groups.push_back(category);
			}

			const auto groupOf = [&groups, &categoryOf](const Product* _product)
			{
				const auto& category = categoryOf(_product);

				if (category.empty())
					return groups.size();

				return static_cast<size_t>(std::find(groups.begin(), groups.end(), category) - groups.begin());
			};

			std::stable_sort(rows.begin(), rows.end(), [&groupOf](const Product* _a, const Product* _b)
			{
				if (groupOf(_a) != groupOf(_b))
					return groupOf(_a) < groupOf(_b);

				return sortsBefore(*_a, *_b);
			});

			// A group folds up by hiding the rows between its heading and the next one. They stay in
			// one flat list rather than going into a box per group: a flex column inside this one is
			// what collapses the rows to a sliver in this RmlUi build, see the body in the stylesheet.
			const auto fold = [](Rml::Element* _heading, const bool _folded)
			{
				_heading->SetClass("collapsed", _folded);

				for (auto* e = _heading->GetNextSibling(); e && !e->IsClassSet("plugin-category"); e = e->GetNextSibling())
					e->SetClass("folded", _folded);
			};

			std::optional<std::string> group;
			std::vector<Rml::Element*> headings;

			for (const auto* product : rows)
			{
				if (!groups.empty() && categoryOf(product) != group)
				{
					group = categoryOf(product);

					const auto count = std::count_if(rows.begin(), rows.end(), [&](const Product* _p)
					{
						return categoryOf(_p) == *group;
					});

					auto* heading = container->AppendChild(_doc.CreateElement("div"));
					heading->SetClassNames("plugin-category");
					heading->SetAttribute("category", *group);
					heading->SetInnerRML("<span class=\"category-caret\"/>" +
						Rml::StringUtilities::EncodeRml(group->empty() ? "Other" : *group) +
						"<span class=\"category-count\">" + std::to_string(count) + "</span>");

					// Remembered with the other settings, so a group stays folded across refreshes and
					// restarts. The list is not rebuilt for it: only the rows' visibility changes.
					juceRmlUi::EventListener::Add(heading, Rml::EventId::Click, [this, heading, fold, category = *group](Rml::Event&)
					{
						const auto folded = !heading->IsClassSet("collapsed");
						fold(heading, folded);

						auto& p = m_pluginManager->getInstallPaths();
						p.setCollapsed(category, folded);
						p.save();
					});

					headings.push_back(heading);
				}

				createEntry(*product, _pluginManager, *container, _doc, formats);
				++shown;
			}

			// Once the rows are there to hide.
			for (auto* heading : headings)
				fold(heading, _pluginManager.getInstallPaths().isCollapsed(heading->GetAttribute<Rml::String>("category", "")));

			setStatusInternal(_doc, summary(shown));

			buildFormatToggles(_doc, formats);
			updateBulkButtons();

			createPreferencesPage(_doc, _pluginManager);
		});

		// A refresh can take the product being shown away with it - a team logout hides everything
		// that was only visible with it - so the detail page follows the catalog rather than showing
		// something that is no longer on offer.
		if (m_page == Page::Detail && m_detail)
		{
			if (const auto* product = catalog.findProduct(m_detail->getProductId()))
				showDetail(*product);
			else
				showPage(Page::List);
		}

		downloadThumbnails(catalog);

		// Asked here because this is where a new catalog arrives, which is the only thing that can
		// change the answer.
		updateSelfUpdateButton(catalog);
	}

	void InstallerDocument::updateSelfUpdateButton(const Catalog& _catalog)
	{
		if (!m_selfUpdate)
			return;

		// Decided during the refresh, because deciding it can mean a request and this runs on the
		// message thread. _catalog is the one it was decided from.
		const auto* asset = m_pluginManager ? m_pluginManager->getSelfUpdate() : nullptr;

		withDocument([&](Rml::ElementDocument&)
		{
			m_selfUpdate->SetClass("hidden", asset == nullptr);

			if (!asset)
				return;

			// "Update to 2.2.27" said neither what would be updated - the plugins carry the same number -
			// nor, for a newer build of the version that is running, that there was anything new (EMU-251).
			if (compareVersions(asset->version, g_appVersion) == 0)
			{
				m_selfUpdate->SetInnerRML("Update Plugin Manager (new build)");
				const auto from = asset->listed.empty() ? std::string() : ", from " + asset->listed;
				m_selfUpdate->SetAttribute("title", "A newer build of Plugin Manager " + asset->version + " is available"
					+ from);
			}
			else
			{
				m_selfUpdate->SetInnerRML("Update Plugin Manager to " + asset->version);
				m_selfUpdate->SetAttribute("title", "Plugin Manager " + asset->version + " is available, this is " +
					std::string(g_appVersion));
			}
		});
	}

	// Three steps, and the middle one is an ordinary install into the folder this program is in.
	// What is around it is the part that makes it work on a running program: the old one is moved
	// aside so the new one has somewhere to land, and put back if the install does not finish.
	void InstallerDocument::startSelfUpdate()
	{
		if (!m_pluginManager || m_busy)
			return;

		const auto* asset = m_pluginManager->getSelfUpdate();

		if (!asset)
			return;

		std::string error;

		if (!SelfUpdate::canUpdateInPlace(error) || !SelfUpdate::makeRoom(error))
		{
			setStatus(error);
			return;
		}

		setStatus("Updating the Plugin Manager to " + asset->version + "...");

		// Named rather than derived: the product is PluginManager and the file in the archive is
		// called "TUS Plugin Manager", so nothing could find it by the usual rule.
		m_selfUpdater.installAsync(*asset, m_pluginManager->credentialsFor(*asset),
			SelfUpdate::appFolder(), nullptr,
			[this](const bool _success, const std::string& _message)
			{
				if (!_success)
				{
					SelfUpdate::restore();
					setStatus(_message);
					return;
				}

				// Before the restart, because after it this program is gone: what it is now, so the next
				// refresh can tell a build newer than this one from this one.
				if (const auto* installed = m_pluginManager->getSelfUpdate())
				{
					auto copy = *installed;
					m_pluginManager->recordSelfUpdate(copy);
				}

				std::string error;

				// With what this one was started with: the restarted copy is meant to be the same program
				// in the same place, only newer.
				const auto* app = juce::JUCEApplicationBase::getInstance();

				if (!SelfUpdate::restart(app ? app->getCommandLineParameters().toStdString() : std::string(), error))
				{
					setStatus(error);
					return;
				}

				// The new one is coming up and this one is in its way - the window, the settings file
				// and, when it is on, the port the MCP server sits on.
				juce::JUCEApplication::quit();
			}, SelfUpdate::appFileName());
	}

	void InstallerDocument::createEntry(const Product& _product, PluginManager& _pluginManager,
	                                    Rml::Element& _container, Rml::ElementDocument& _doc,
	                                    const std::vector<std::string>& _formats)
	{
		auto* templateEntry = _doc.GetElementById("plugin-entry-template");

		if (!templateEntry)
			return;

		auto* entry = _container.AppendChild(_doc.CreateElement("div"));

		if (!entry)
			return;

		entry->SetClassNames("plugin-entry");

		// So that a right-click anywhere inside the row can say which product it landed on: the
		// event arrives at whatever label was under the cursor, and walks up to find this.
		entry->SetAttribute("product", _product.product);
		entry->SetInnerRML(templateEntry->GetInnerRML());

		auto controller = std::make_unique<PluginEntryController>(_product, _pluginManager, entry, *m_rmlComponent,
			[this](const Product& _clicked)
			{
				post([this, _clicked] { showDetail(_clicked); });
			}, _formats);

		if (!_product.thumbnail.empty())
		{
			const auto filename = juce::URL(_product.thumbnail).getFileName().toStdString();

			controller->setThumbnailFilename(filename);

			// Rows are recreated on every refresh, and an image fetched for an earlier row is cached rather
			// than downloaded again - so it is applied here.
			if (m_dataProvider.hasImage(filename))
				controller->updateThumbnail();
		}

		// Before the row starts reporting changes, so that putting it back the way it was is not
		// mistaken for the user doing it.
		applySelection(*controller);

		controller->setOnChanged([this] { updateBulkButtons(); });

		m_entries.push_back(std::move(controller));
	}

	void InstallerDocument::setProblem(std::string _problem)
	{
		m_problem = std::move(_problem);
	}

	// The row under the cursor, if the click landed on one. The event arrives at whatever was
	// directly under it - a label, a button, the thumbnail - so the row is found by walking up.
	void InstallerDocument::openListMenu(Rml::Event& _event)
	{
		if (!m_pluginManager || !m_pluginList)
			return;

		auto& paths = m_pluginManager->getInstallPaths();

		std::string product;

		for (auto* e = _event.GetTargetElement(); e && e != m_pluginList; e = e->GetParentNode())
		{
			product = e->GetAttribute<Rml::String>("product", "");

			if (!product.empty())
				break;
		}

		juceRmlUi::Menu menu;

		if (!product.empty())
		{
			const auto* entry = m_pluginManager->getCatalog().findProduct(product);
			const auto name = entry ? entry->name : product;

			menu.addEntry("Hide " + name, [this, product]
			{
				// Posted rather than done here: the rebuild takes away the elements this menu was
				// opened from, and this is still inside their event handler.
				post([this, product]
				{
					auto& p = m_pluginManager->getInstallPaths();
					p.setHidden(product, true);
					p.save();
					rebuild(*m_pluginManager);
				});
			});
		}

		if (const auto hidden = paths.getHiddenCount())
		{
			const auto what = hidden == 1 ? std::string("the hidden plugin")
				: "all " + std::to_string(hidden) + " hidden plugins";

			menu.addEntry("Show " + what, [this]
			{
				post([this]
				{
					auto& p = m_pluginManager->getInstallPaths();
					p.showAll();
					p.save();
					rebuild(*m_pluginManager);
				});
			});
		}

		// Nothing to offer on an empty list with nothing hidden, and an empty menu that opens
		// anyway reads as a broken one.
		if (menu.empty())
			return;

		menu.runModal(_event);
	}

	// A count on its own says the catalog is complete. When it is not, the reason belongs next to
	// it - and when there is nothing to count at all, the reason is the whole of the answer:
	// "no builds for this platform" is what a working program says on a platform we do not build
	// for, and must not be what a program that could not reach anything says.
	std::string InstallerDocument::summary(const size_t _shown) const
	{
		// Said even when it is the whole list, because a window with no rows in it and no
		// explanation is the one way this feature can look like a bug.
		const auto hidden = m_hiddenCount ? ", " + std::to_string(m_hiddenCount) + " hidden" : std::string();

		if (!_shown)
		{
			if (m_hiddenCount)
				return "Every plugin is hidden. Right-click the list to bring them back.";

			return m_problem.empty() ? "No builds available for this platform" : m_problem;
		}

		const auto count = std::to_string(_shown) + (_shown == 1 ? " plugin" : " plugins") + hidden;

		return m_problem.empty() ? count : count + ". " + m_problem;
	}

	void InstallerDocument::setStatus(const std::string& _text)
	{
		withDocument([&](Rml::ElementDocument& _doc)
		{
			setStatusInternal(_doc, _text);
		});
	}

	void InstallerDocument::setBusy(const bool _busy)
	{
		m_busy = _busy;

		withDocument([&](Rml::ElementDocument& _doc)
		{
			if (auto* bar = _doc.GetElementById("status-bar"))
				bar->SetClass("busy", _busy);

			if (m_refreshButton)
				m_refreshButton->SetClass("busy", _busy);
		});
	}

	void InstallerDocument::setStatusInternal(Rml::ElementDocument& _doc, const std::string& _text)
	{
		if (auto* statusText = _doc.GetElementById("status-text"))
			statusText->SetInnerRML(_text);
	}

	// Whether there is a team login at all hides the whole section rather than the button in it,
	// since a heading on its own would only ask what it is for. The button says what pressing it
	// does, and wears the colour the rest of the program gives that: green adds, red takes away.
	void InstallerDocument::updateTeamLogin() const
	{
		if (m_teamLoginSection)
			m_teamLoginSection->SetClass("hidden", !m_teamLoginAvailable);

		if (!m_teamLogin)
			return;

		m_teamLogin->SetInnerRML(m_teamLoggedIn ? "Log out" : "Log in");
		m_teamLogin->SetClass("primary", !m_teamLoggedIn);
		m_teamLogin->SetClass("danger", m_teamLoggedIn);
	}

	void InstallerDocument::setTeamLoginAvailable(const bool _available)
	{
		m_teamLoginAvailable = _available;

		withDocument([this](Rml::ElementDocument&) { updateTeamLogin(); });
	}

	void InstallerDocument::setTeamLoggedIn(const bool _loggedIn)
	{
		m_teamLoggedIn = _loggedIn;

		withDocument([this](Rml::ElementDocument&) { updateTeamLogin(); });
	}

	void InstallerDocument::openLogin(const std::string& _title, const bool _withFolder, LoginDialog::SubmitCallback _onSubmit)
	{
		withDocument([&](Rml::ElementDocument&)
		{
			// The dialog calls this from its own event handler, so the app is told from outside of it.
			m_login->open(_title, _withFolder,
				[onSubmit = std::move(_onSubmit)](const std::string& _folder, const Credentials& _credentials)
			{
				post([onSubmit, _folder, _credentials]
				{
					onSubmit(_folder, _credentials);
				});
			});
		});
	}

	void InstallerDocument::loginFailed(const std::string& _message)
	{
		withDocument([&](Rml::ElementDocument&)
		{
			m_login->failed(_message);
		});
	}

	void InstallerDocument::closeLogin()
	{
		withDocument([&](Rml::ElementDocument&)
		{
			m_login->close();
		});
	}

	// By the file that arrived rather than by the product that fetched it: one picture can belong
	// to several rows - a test console is shown with the picture of the plugin it tests - and the
	// loop below only ever downloads it once.
	void InstallerDocument::onThumbnailReady(const std::string& _filename)
	{
		withDocument([&](Rml::ElementDocument&)
		{
			for (auto& entry : m_entries)
			{
				if (entry->getThumbnailFilename() == _filename)
					entry->updateThumbnail();
			}

			// The detail page may be showing a product the image just arrived for.
			if (m_detail && m_detail->getThumbnailFilename() == _filename)
				m_detail->updateThumbnail();
		});
	}

	void InstallerDocument::downloadThumbnails(const Catalog& _catalog)
	{
		for (const auto& product : _catalog.getProducts())
		{
			// Nothing shows it, so fetching it is a download that is thrown away on every start.
			if (product.thumbnail.empty() || isAppProduct(product.product))
				continue;

			const auto filename = juce::URL(product.thumbnail).getFileName().toStdString();

			if (filename.empty() || m_dataProvider.hasImage(filename))
				continue;

			// Once per picture, not once per product that wants it. Seven of them are shared, and
			// every sharer used to start a download of its own into the one file named after it. They
			// wrote over each other, so what arrived either would not decode - the magenta square the
			// renderer puts there instead - or was gone altogether, because the first one to finish
			// deleted the file while the others were still writing it. Windows hid this: the second
			// writer cannot even open a file the first one holds, so its download simply failed and
			// left the good copy alone. macOS lets them both write, which is why it showed up there.
			if (!m_thumbnailsWanted.insert(filename).second)
				continue;

			// And a name nothing else can be given, so that two downloads cannot land on one file
			// however the loop above changes.
			const auto tempPath = juce::File::createTempFile(".tusthumbnail").getFullPathName().toStdString();

			// One line per picture fetched, which is what says the line above is doing its job: a name
			// twice in here would mean two downloads of one file again.
			Log::write("Thumbnail " + filename + " from " + product.thumbnail);

			// With whatever login reaches it: a picture for a plugin nobody has published sits in the
			// same protected folder its builds do.
			const auto credentials = m_pluginManager ? m_pluginManager->credentialsForThumbnail(product) : Credentials{};

			HttpClient::downloadFileAsync(product.thumbnail, tempPath, nullptr,
				[this, filename, tempPath, alive = m_alive](const bool _success, const std::string&)
				{
					const auto file = juce::File(tempPath);

					// The download outlives the window it was started for if that is closed while it runs,
					// and then there is nothing here to put the picture into - only a file to take away.
					if (!*alive)
					{
						file.deleteFile();
						return;
					}

					// Whether it worked or not, so that a Refresh may ask for this one again.
					m_thumbnailsWanted.erase(filename);

					juce::MemoryBlock data;

					if (_success && file.loadFileAsData(data))
					{
						std::vector<char> imageData(data.getSize());
						std::memcpy(imageData.data(), data.getData(), data.getSize());
						m_dataProvider.addImage(filename, std::move(imageData));

						onThumbnailReady(filename);
					}

					file.deleteFile();
				},
				nullptr, credentials);
		}
	}
}
