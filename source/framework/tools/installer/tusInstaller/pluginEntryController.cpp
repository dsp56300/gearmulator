#include "pluginEntryController.h"

#include "bulkUninstall.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlMenu.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"

#include <JuceHeader.h>

namespace pluginManager
{
	namespace
	{
		// What the button in a format column carries. An SVG per action, which the stylesheet tints
		// to whatever that button is saying: the font here has no arrow and no trash can, and a
		// stack of boxes was never going to look like one.
		std::string iconHtml(const InstallAction _action)
		{
			switch (_action)
			{
			case InstallAction::Upgrade:
				return R"(<svg class="btn-icon" src="icon_upgrade.svg"/>)";
			case InstallAction::Downgrade:
				return R"(<svg class="btn-icon" src="icon_downgrade.svg"/>)";
			case InstallAction::Reinstall:
				return R"(<svg class="btn-icon" src="icon_reinstall.svg"/>)";
			default:
				return R"(<svg class="btn-icon" src="icon_install.svg"/>)";
			}
		}

		// The same, in words, for the tooltip.
		std::string actionTitle(const InstallAction _action, const std::string& _format, const std::string& _version)
		{
			switch (_action)
			{
			case InstallAction::Upgrade:   return "Upgrade " + _format + " to " + _version;
			case InstallAction::Downgrade: return "Downgrade " + _format + " to " + _version;
			case InstallAction::Reinstall: return "Reinstall " + _format + " " + _version;
			default:                       return "Install " + _format + " " + _version;
			}
		}
	}

	const std::string& PluginEntryController::FormatRow::selectedVersion() const
	{
		static const std::string empty;

		return versionIndex < versionList.size() ? versionList[versionIndex] : empty;
	}

	// A click that is answered here must not also reach the row, which would open the detail page on
	// top of what was clicked.
	void PluginEntryController::claimClick(Rml::Element* _element, const std::function<void(Rml::Event&)>& _handler)
	{
		if (!_element)
			return;

		juceRmlUi::EventListener::Add(_element, Rml::EventId::Click, [_handler](Rml::Event& _event)
		{
			_event.StopPropagation();
			_handler(_event);
		});
	}

	PluginEntryController::PluginEntryController(Product _product, PluginManager& _pluginManager, Rml::Element* _element,
	                                             juceRmlUi::RmlComponent& _rmlComponent, ProductRequest _onShowDetails,
	                                             std::vector<std::string> _allFormats)
		: m_product(std::move(_product))
		, m_pluginManager(_pluginManager)
		, m_catalog(_pluginManager.getCatalog())
		, m_allFormats(std::move(_allFormats))
		, m_element(_element)
		, m_rmlComponent(_rmlComponent)
		, m_onShowDetails(std::move(_onShowDetails))
		, m_installer(_pluginManager, _rmlComponent)
	{
		m_thumbnailImg = m_element->QuerySelector(".thumbnail-image");
		m_nameElem = m_element->QuerySelector(".plugin-name");
		m_descElem = m_element->QuerySelector(".plugin-description");
		m_progressDiv = m_element->QuerySelector(".plugin-progress");
		m_progressBar = m_element->QuerySelector(".progress-bar");
		m_cancelButton = m_element->QuerySelector(".cancel-button");
		m_statusElem = m_element->QuerySelector(".plugin-status");

		m_fxProduct = m_catalog.fxProductOf(m_product.product);

		populate();
		addFormatRows();
		addAllColumn();
		setupFxToggle();

		claimClick(m_cancelButton, [this](Rml::Event&) { onCancelClicked(); });

		// Anything else on the row opens the product in full.
		juceRmlUi::EventListener::Add(m_element, Rml::EventId::Click, [this](Rml::Event&)
		{
			if (m_onShowDetails)
				m_onShowDetails(m_product);
		});

		updateAllColumn();
	}

	PluginEntryController::~PluginEntryController()
	{
		*m_alive = false;
	}

	void PluginEntryController::setThumbnailFilename(std::string _filename)
	{
		m_thumbnailFilename = std::move(_filename);
	}

	void PluginEntryController::refreshInstalled()
	{
		for (auto& row : m_formats)
		{
			refreshInstalledState(row);
			updateRow(row);
		}

		// Which also tells the buttons above the list, through the row having changed.
		updateAllColumn();
	}

	void PluginEntryController::updateThumbnail()
	{
		if (m_thumbnailImg && !m_thumbnailFilename.empty())
			m_thumbnailImg->SetAttribute("src", m_thumbnailFilename);
	}

	// The effect builds go into the same folder as the plugin, under the same login - which
	// findOwningProduct already arranges, since they are named after it. All that was missing was
	// somewhere to ask for them.
	void PluginEntryController::setupFxToggle()
	{
		m_fxToggle = m_element->QuerySelector(".fx-toggle");

		if (!m_fxToggle)
			return;

		// Nothing to offer for this plugin, so the chip is not in the row rather than in it and
		// doing nothing.
		if (m_fxProduct.empty())
		{
			m_fxToggle->SetProperty("display", "none");
			m_fxToggle = nullptr;
			return;
		}

		m_fxToggle->SetClass("checked", m_fxSelected);

		claimClick(m_fxToggle, [this](Rml::Event&) { setFxSelected(!m_fxSelected); });
	}

	void PluginEntryController::setFxSelected(const bool _selected)
	{
		if (m_fxSelected == _selected)
			return;

		m_fxSelected = _selected;

		// Not there at all on a plugin without an effect build, which is the one case where this is
		// called with nothing to show for it.
		if (m_fxToggle)
			m_fxToggle->SetClass("checked", m_fxSelected);

		// Every column says something different now: what counts as installed, and what its
		// button would do.
		for (auto& row : m_formats)
		{
			refreshInstalledState(row);
			updateRow(row);
		}

		updateAllColumn();
	}

	bool PluginEntryController::hasFxBuild(const FormatRow& _row) const
	{
		if (m_fxProduct.empty())
			return false;

		return m_catalog.find(m_fxProduct, _row.format, _row.selectedVersion(),
			PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch()) != nullptr;
	}

	InstallAction PluginEntryController::actionFor(const FormatRow& _row) const
	{
		const auto plugin = m_pluginManager.installAction(m_product.product, _row.format, _row.installed,
			_row.installedVersion, _row.selectedVersion());

		if (!wantsFx() || !hasFxBuild(_row))
			return plugin;

		const auto fx = m_pluginManager.installAction(m_fxProduct, _row.format, _row.fxInstalled,
			_row.fxInstalledVersion, _row.selectedVersion());

		return combineInstallActions(plugin, fx);
	}

	void PluginEntryController::populate()
	{
		if (m_nameElem)
			m_nameElem->SetInnerRML(m_product.name.empty() ? m_product.product : m_product.name);

		if (m_descElem)
			m_descElem->SetInnerRML(m_product.description);
	}

	// A column per format on offer here, whether or not this plugin comes in it: an empty column
	// keeps the ones after it under the same formats on every other row.
	void PluginEntryController::addFormatRows()
	{
		auto* container = m_element->QuerySelector(".plugin-formats");

		if (!container)
			return;

		auto* doc = m_element->GetOwnerDocument();

		if (!doc)
			return;

		auto* columnTemplate = doc->GetElementById("format-column-template");

		if (!columnTemplate)
			return;

		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		for (const auto& format : m_allFormats)
		{
			auto* column = container->AppendChild(doc->CreateElement("div"));

			if (!column)
				continue;

			column->SetClassNames("format-col");

			FormatRow row;
			row.format = format;
			row.versionList = m_catalog.getVersions(m_product.product, format, os, arch);

			if (row.versionList.empty())
				continue;

			column->SetInnerRML(columnTemplate->GetInnerRML());

			row.column = column;
			row.label = column->QuerySelector(".format-label");

			// Once: the chip says which format the column is for, and that does not change while the
			// row exists. Rewriting it on every refresh is what used to take the state icons with it.
			if (auto* name = column->QuerySelector(".format-name"))
				name->SetInnerRML(format);
			row.installedLabel = column->QuerySelector(".format-installed");
			row.versionLabel = column->QuerySelector(".version-select");
			row.actionButton = column->QuerySelector(".format-action");

			refreshInstalledState(row);
			updateRow(row);

			const auto rowIndex = m_formats.size();

			claimClick(row.versionLabel, [this, rowIndex](Rml::Event& _event) { openVersionMenu(rowIndex, _event); });
			claimClick(row.actionButton, [this, rowIndex](Rml::Event&) { onFormatAction(rowIndex); });
			claimClick(column->QuerySelector(".format-remove"), [this, rowIndex](Rml::Event&) { onFormatRemove(rowIndex); });

			claimClick(row.label, [this, rowIndex](Rml::Event&)
			{
				auto& r = m_formats[rowIndex];
				r.selected = !r.selected;
				r.label->SetClass("checked", r.selected);
				updateAllColumn();
			});

			m_formats.push_back(std::move(row));
		}
	}

	// Last, after the formats it summarises.
	void PluginEntryController::addAllColumn()
	{
		auto* container = m_element->QuerySelector(".plugin-formats");
		auto* doc = m_element->GetOwnerDocument();

		if (!container || !doc || m_formats.empty())
			return;

		auto* columnTemplate = doc->GetElementById("format-all-template");

		if (!columnTemplate)
			return;

		auto* column = container->AppendChild(doc->CreateElement("div"));

		if (!column)
			return;

		column->SetClassNames("format-col all");
		column->SetInnerRML(columnTemplate->GetInnerRML());

		m_allSummary = column->QuerySelector(".all-summary");
		m_allInstall = column->QuerySelector(".all-install");
		m_allUpgrade = column->QuerySelector(".all-upgrade");
		m_allRemove = column->QuerySelector(".all-remove");

		claimClick(m_allInstall, [this](Rml::Event&) { onBulk(Bulk::Install); });
		claimClick(m_allUpgrade, [this](Rml::Event&) { onBulk(Bulk::Upgrade); });
		claimClick(m_allRemove, [this](Rml::Event&) { onBulk(Bulk::Remove); });
	}

	// Ticking is what says which formats a row is about, so the All column goes by it too: a format
	// nobody wants here is not installed by the button that installs the rest.
	std::vector<size_t> PluginEntryController::formatsFor(const Bulk _what) const
	{
		std::vector<size_t> formats;

		for (size_t i = 0; i < m_formats.size(); ++i)
		{
			const auto& row = m_formats[i];

			// What is installed is wanted, ticked or not: with nothing ticked until somebody ticks it
			// (EMU-248), going by the ticks would have left "Upgrade all" with nothing to do. Installing
			// and removing still go by them.
			if (!row.selected && _what != Bulk::Upgrade)
				continue;

			const auto action = actionFor(row);

			// Removing takes away whatever of the two is there, and the other two go by what the
			// column now offers - which already takes the effect build into account.
			const auto applies = _what == Bulk::Remove
				? (row.installed || (wantsFx() && row.fxInstalled))
				: (_what == Bulk::Install ? action == InstallAction::Install : action == InstallAction::Upgrade);

			if (applies)
				formats.push_back(i);
		}

		return formats;
	}

	// A build that is not public says where it comes from, so an internal build is never picked in the
	// belief that it is a release. The column has room for a letter rather than the word; the menu it
	// opens spells it out.
	std::string PluginEntryController::versionText(const FormatRow& _row, const size_t _index, const bool _short) const
	{
		const auto& version = _row.versionList[_index];

		const auto channel = m_catalog.getChannel(m_product.product, _row.format, version,
		                                         PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch());

		switch (channel)
		{
		case Channel::Internal: return version + (_short ? " I" : " (internal)");
		case Channel::Donators: return version + (_short ? " D" : " (donators)");
		default:                return version;
		}
	}

	// The chip that opens the version menu. The arrow is what says it is one, and is left off when
	// there is only one version to pick from.
	void PluginEntryController::setVersionLabel(const FormatRow& _row) const
	{
		if (!_row.versionLabel)
			return;

		_row.versionLabel->SetInnerRML(versionText(_row, _row.versionIndex, true) +
			(_row.versionList.size() > 1 ? "<span class=\"version-caret\"/>" : ""));
	}

	// Newest first, so the top entry is the default and everything under it is a rollback.
	void PluginEntryController::openVersionMenu(const size_t _index, Rml::Event& _event)
	{
		auto& row = m_formats[_index];

		if (row.versionList.size() < 2)
			return;

		juceRmlUi::Menu menu;

		for (size_t i = 0; i < row.versionList.size(); ++i)
		{
			menu.addEntry(versionText(row, i), i == row.versionIndex, [this, _index, i]
			{
				auto& r = m_formats[_index];
				r.versionIndex = i;

				// Picking an older one than what is installed turns the button into a downgrade, so
				// the whole column is written again rather than only its version - and the All
				// column with it, since what it offers follows what the columns say.
				updateRow(r);
				updateAllColumn();

				m_rmlComponent.enqueueUpdate();
			});
		}

		menu.runModal(_event);
	}

	void PluginEntryController::setAllFormatsSelected(const bool _selected)
	{
		for (auto& row : m_formats)
		{
			if (row.selected == _selected)
				continue;

			row.selected = _selected;

			if (row.label)
				row.label->SetClass("checked", _selected);
		}

		updateAllColumn();
	}

	bool PluginEntryController::areAllFormatsSelected() const
	{
		for (const auto& row : m_formats)
		{
			if (!row.selected)
				return false;
		}

		return true;
	}

	bool PluginEntryController::offersFormat(const std::string& _format) const
	{
		for (const auto& row : m_formats)
		{
			if (row.format == _format)
				return true;
		}

		return false;
	}

	bool PluginEntryController::isFormatSelected(const std::string& _format) const
	{
		for (const auto& row : m_formats)
		{
			if (row.format == _format)
				return row.selected;
		}

		return true;
	}

	std::vector<std::string> PluginEntryController::getSelectedFormats() const
	{
		std::vector<std::string> formats;

		for (const auto& row : m_formats)
		{
			if (row.selected)
				formats.push_back(row.format);
		}

		return formats;
	}

	void PluginEntryController::setFormatSelected(const std::string& _format, const bool _selected)
	{
		for (auto& row : m_formats)
		{
			if (row.format != _format || row.selected == _selected)
				continue;

			row.selected = _selected;

			if (row.label)
				row.label->SetClass("checked", _selected);
		}

		updateAllColumn();
	}

	void PluginEntryController::refreshInstalledState(FormatRow& _row) const
	{
		_row.installed = m_pluginManager.isInstalled(m_product.product, _row.format);

		_row.installedVersion = _row.installed
			? m_pluginManager.getInstalledVersion(m_product.product, _row.format)
			: std::string();

		_row.fxInstalled = false;
		_row.fxInstalledVersion.clear();

		if (m_fxProduct.empty())
			return;

		// The same folder as the plugin, which is what asking for the effect build's own location
		// answers - it has none of its own.
		_row.fxInstalled = m_pluginManager.isInstalled(m_fxProduct, _row.format);

		if (_row.fxInstalled)
			_row.fxInstalledVersion = m_pluginManager.getInstalledVersion(m_fxProduct, _row.format);
	}

	// Everything in one column: the chip, the version that is on the disk, the version that would be
	// installed, and which of the four things the button would do.
	void PluginEntryController::updateRow(const FormatRow& _row) const
	{
		if (!_row.label)
			return;

		_row.label->SetClass("installed", _row.installed);

		// Installed, but not what is on offer. Only said when the installed version is known: a
		// format that cannot tell us is left looking plainly installed rather than wrongly outdated.
		const auto newest = _row.versionList.empty() ? std::string() : _row.versionList.front();

		// A newer version number, or - when the number is the same - a newer file, or a copy that cannot
		// say its version and is older than what is on offer. See PluginManager::isOutdated.
		const auto outdated = _row.installed &&
			m_pluginManager.isOutdated(m_product.product, _row.format, _row.installedVersion, newest);

		_row.label->SetClass("outdated", outdated);

		// The state at a glance beside the name: a tick for the newest, an arrow for one that has a
		// newer version waiting. Neither for a build that cannot say which version it is, for the same
		// reason as above. Which icon that means is the stylesheet's to decide - both are in the
		// template and this only says what the chip is.
		const auto current = _row.installed && !_row.installedVersion.empty() && !outdated;

		_row.label->SetClass("current", current);

		_row.label->SetAttribute("title", current ? _row.format + " " + _row.installedVersion + " is the newest version"
			: (outdated ? _row.format + " " + _row.installedVersion + " is installed, " + newest + " is available"
				: std::string()));

		if (_row.column)
			_row.column->SetClass("is-installed", _row.installed);

		if (_row.installedLabel)
		{
			// An installed build that cannot say which version it is says so, rather than being left
			// looking uninstalled.
			_row.installedLabel->SetInnerRML(_row.installed
				? (_row.installedVersion.empty() ? "installed" : _row.installedVersion)
				: "-");

			_row.installedLabel->SetClass("current", _row.installed && !outdated);
			_row.installedLabel->SetClass("outdated", outdated);
		}

		setVersionLabel(_row);

		if (!_row.actionButton)
			return;

		const auto action = actionFor(_row);

		_row.actionButton->SetClass("install", action == InstallAction::Install);
		_row.actionButton->SetClass("reinstall", action == InstallAction::Reinstall);
		_row.actionButton->SetClass("upgrade", action == InstallAction::Upgrade);
		_row.actionButton->SetClass("downgrade", action == InstallAction::Downgrade);

		_row.actionButton->SetInnerRML(iconHtml(action));
		_row.actionButton->SetAttribute("title", actionTitle(action, _row.format, _row.selectedVersion()));
	}

	// How much of the row is installed, and which of the three things are worth offering. An action
	// with nothing to act on stays visible but dim: the column keeps its shape down the whole list.
	void PluginEntryController::updateAllColumn()
	{
		if (!m_allSummary)
			return;

		size_t installed = 0;
		size_t selected = 0;

		for (const auto& row : m_formats)
		{
			if (!row.selected)
				continue;

			++selected;

			if (row.installed)
				++installed;
		}

		m_allSummary->SetInnerRML(std::to_string(installed) + " of " + std::to_string(selected));
		m_allSummary->SetClass("current", installed > 0 && installed == selected);

		const auto ready = [](Rml::Element* _button, const bool _any)
		{
			if (_button)
				_button->SetClass("ready", _any);
		};

		ready(m_allInstall, !formatsFor(Bulk::Install).empty());
		ready(m_allUpgrade, !formatsFor(Bulk::Upgrade).empty());
		ready(m_allRemove, !formatsFor(Bulk::Remove).empty());

		// The buttons above the list count every row, so they are told whenever one of them moves.
		if (m_onChanged)
			m_onChanged();
	}

	// The same action, asked for from above the list. Whoever asked is walking the rows one after
	// another, so it is told when this one is through whatever the reason.
	void PluginEntryController::startBulk(const Bulk _what, std::function<void()> _onDone,
	                                      PluginInstaller::Pending _shared)
	{
		const auto formats = _what == Bulk::Remove ? std::vector<size_t>() : formatsFor(_what);

		if (formats.empty() || m_installer.isRunning())
		{
			if (_onDone)
				_onDone();

			return;
		}

		m_onQueueDone = std::move(_onDone);

		// Somebody else is collecting, and will ask once for all of the rows rather than once here.
		m_elevated = std::move(_shared);
		m_ownsElevated = m_elevated == nullptr;

		startInstalls(formats);
	}

	// Install, upgrade or remove every ticked format the action applies to.
	void PluginEntryController::onBulk(const Bulk _what)
	{
		const auto formats = formatsFor(_what);

		if (formats.empty())
			return;

		if (_what != Bulk::Remove)
		{
			startInstalls(formats, true);
			return;
		}

		std::vector<std::string> names;

		for (const auto index : formats)
			names.push_back(m_formats[index].format);

		confirmUninstall(m_rmlComponent, m_product.name.empty() ? m_product.product : m_product.name, names,
			m_alive, [this, formats, names]
			{
				// The effect builds go with them, and are not counted: a format may well have one and
				// not the other, and what is asked for here is that neither is left behind. All of it in
				// one call, so a system folder asks for administrator rights once (EMU-221).
				std::vector<std::pair<std::string, std::string>> items;

				if (wantsFx())
				{
					for (const auto& name : names)
					{
						if (m_pluginManager.isInstalled(m_fxProduct, name))
							items.emplace_back(m_fxProduct, name);
					}
				}

				const auto first = items.size();

				for (const auto& name : names)
					items.emplace_back(m_product.product, name);

				std::string message;

				const auto results = m_pluginManager.uninstallMany(items, message);
				const auto removed = static_cast<size_t>(std::count(results.begin() + static_cast<std::ptrdiff_t>(first), results.end(), true));

				if (message.empty())
					message = "Removed " + std::to_string(removed) + (removed == 1 ? " format" : " formats");

				for (const auto index : formats)
				{
					auto& row = m_formats[index];
					refreshInstalledState(row);
					updateRow(row);
				}

				updateAllColumn();

				setStatus(removed == names.size() ? message : "Failed: " + message);

				if (removed == names.size())
					return;

				juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
					"Could not remove everything", juce::String(message));
			});
	}

	void PluginEntryController::startInstalls(const std::vector<size_t>& _formats, const bool _skipUpToDate)
	{
		if (m_installer.isRunning() || _formats.empty())
			return;

		// Worth doing at all: a column asked for on its own is fetched whatever state it is in,
		// because that is what pressing its button means, but the buttons that walk everything must
		// not fetch a plugin again only because its effect build was missing.
		const auto worthDoing = [&](const std::string& _product, const bool _installed, const std::string& _version,
		                            const FormatRow& _row)
		{
			if (!_skipUpToDate)
				return true;

			const auto action = m_pluginManager.installAction(_product, _row.format, _installed, _version,
				_row.selectedVersion());

			return action == InstallAction::Install || action == InstallAction::Upgrade;
		};

		std::vector<Job> jobs;

		for (const auto index : _formats)
		{
			const auto& row = m_formats[index];

			if (worthDoing(m_product.product, row.installed, row.installedVersion, row))
				jobs.push_back({index, false});

			if (wantsFx() && hasFxBuild(row) && worthDoing(m_fxProduct, row.fxInstalled, row.fxInstalledVersion, row))
				jobs.push_back({index, true});
		}

		if (jobs.empty())
			return;

		m_pending = std::move(jobs);

		// One per run, so everything this walk unpacks asks for its rights together. Unless a run over
		// several rows already handed one in, which is collecting for all of them.
		if (!m_elevated)
		{
			m_elevated = std::make_shared<ElevatedBatch>();
			m_ownsElevated = true;
		}

		showProgress(true);
		installNext();
	}

	// The button in one column, whatever its icon says: all four of them install the version that is
	// picked there, over whatever is on the disk.
	void PluginEntryController::onFormatAction(const size_t _index)
	{
		startInstalls({_index});
	}

	void PluginEntryController::onFormatRemove(const size_t _index)
	{
		auto& row = m_formats[_index];

		const auto alsoFx = wantsFx() && row.fxInstalled;

		if (!row.installed && !alsoFx)
			return;

		// Both, while the chip is ticked: leaving the effect build behind after taking the plugin
		// away is how a folder ends up holding something nobody asked to keep. In one call, so a
		// system folder asks for administrator rights once for the two (EMU-221).
		std::vector<std::pair<std::string, std::string>> items;

		if (alsoFx)
			items.emplace_back(m_fxProduct, row.format);

		if (row.installed)
			items.emplace_back(m_product.product, row.format);

		std::string failure;

		const auto results = m_pluginManager.uninstallMany(items, failure);
		const auto removed = std::find(results.begin(), results.end(), false) == results.end();
		const auto message = removed ? items.back().first + " " + row.format + " removed" : failure;

		setStatus(removed ? message : "Failed: " + message);

		refreshInstalledState(row);
		updateRow(row);
		updateAllColumn();

		if (removed)
			return;

		juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
			juce::String(m_product.product) + ": could not remove " + juce::String(row.format),
			juce::String(message));
	}

	// Whoever started this queue from above the list is waiting for it, and gets its answer whether
	// the walk ran out of formats or stopped on a failure.
	void PluginEntryController::finishQueue()
	{
		if (auto done = std::move(m_onQueueDone))
		{
			m_onQueueDone = nullptr;
			done();
		}
	}

	void PluginEntryController::refreshAll()
	{
		for (auto& row : m_formats)
		{
			refreshInstalledState(row);
			updateRow(row);
		}

		updateAllColumn();
	}

	void PluginEntryController::finishElevated()
	{
		// Not this row's to ask for: the run it belongs to does it once, when every row is through.
		if (!m_ownsElevated)
			return;

		if (!m_elevated || m_elevated->empty())
		{
			setStatus({});
			return;
		}

		setStatus("Waiting for administrator rights...");

		std::string error;
		const auto done = m_pluginManager.finishPending(*m_elevated, error);

		// Whatever happened, the rows are told by the disk rather than by the install that asked.
		refreshAll();

		setStatus(done ? std::string() : "Failed: " + error);
	}

	void PluginEntryController::installNext()
	{
		if (m_pending.empty())
		{
			finishElevated();

			showProgress(false);
			updateAllColumn();
			finishQueue();
			return;
		}

		const auto job = m_pending.front();
		m_pending.erase(m_pending.begin());

		const auto index = job.format;
		const auto& row = m_formats[index];
		const auto version = row.selectedVersion();
		const auto& product = job.fx ? m_fxProduct : m_product.product;

		const auto* asset = m_catalog.find(product, row.format, version,
		                                   PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch());

		if (!asset)
		{
			setStatus("No download for " + product + " " + row.format + " " + version);
			installNext();
			return;
		}

		// Named, because two of these going by in one run is otherwise two identical lines.
		const auto what = (job.fx ? m_fxProduct + " " : std::string()) + row.format + " " + version;

		setStatus("Downloading " + what + "...");

		m_installer.start(*asset, job.fx ? m_fxProduct : (m_product.name.empty() ? m_product.product : m_product.name),
			[this, what](const float _progress)
			{
				setProgress(_progress);

				// The bar alone does not say what is being fetched, and a small build is over so fast
				// that the bar is all there would have been to see.
				if (_progress >= 0.0f)
					setStatus("Downloading " + what + " " + std::to_string(static_cast<int>(_progress * 100.0f)) + "%");
			},
			[this, index](const bool _success, const std::string& _message)
			{
				if (!_success)
				{
					// Stop the run rather than silently carrying on to the next format: the failure
					// is usually the same for all of them (no write access, no network).
					m_pending.clear();

					// What was already unpacked still goes in: the formats that got that far are
					// installable, and leaving them promised would lose them without a word.
					finishElevated();

					setStatus("Failed: " + _message);
					showProgress(false);
					finishQueue();
					return;
				}

				auto& r = m_formats[index];
				refreshInstalledState(r);
				updateRow(r);
				updateAllColumn();

				installNext();
			}, m_elevated);
	}

	void PluginEntryController::onCancelClicked()
	{
		m_pending.clear();
		m_installer.cancel();
		showProgress(false);
		setStatus({});
		finishQueue();
	}

	void PluginEntryController::setProgress(const float _progress)
	{
		if (m_progressBar)
			m_progressBar->SetAttribute("value", _progress < 0.0f ? 50.0f : _progress * 100.0f);
	}

	void PluginEntryController::showProgress(const bool _show)
	{
		if (m_progressDiv)
			m_progressDiv->SetClass("active", _show);
	}

	void PluginEntryController::setStatus(const std::string& _text)
	{
		if (!m_statusElem)
			return;

		m_statusElem->SetInnerRML(_text);

		// Out of the layout entirely while it says nothing. An empty line still takes a line, and
		// that one sat under the name and pushed it above the middle of the picture.
		m_statusElem->SetClass("hidden", _text.empty());
	}
}
