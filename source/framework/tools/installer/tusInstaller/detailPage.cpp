#include "detailPage.h"

#include "pluginManagerLib/backup.h"

#include "bulkUninstall.h"
#include "installerDataProvider.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

#include <algorithm>

#include <JuceHeader.h>

namespace pluginManager
{
	namespace
	{
		std::string channelSuffix(const Channel _channel)
		{
			switch (_channel)
			{
			case Channel::Internal: return " (internal)";
			case Channel::Donators: return " (donators)";
			default:                return " (public)";
			}
		}

		void setText(Rml::Element* _element, const std::string& _text)
		{
			if (_element)
				_element->SetInnerRML(Rml::StringUtilities::EncodeRml(_text));
		}
	}

	DetailPage::DetailPage(Rml::ElementDocument& _doc, InstallerDataProvider& _dataProvider,
	                       juceRmlUi::RmlComponent& _rmlComponent)
		: m_doc(_doc)
		, m_dataProvider(_dataProvider)
		, m_rmlComponent(_rmlComponent)
		, m_page(_doc.GetElementById("detail-page"))
		, m_back(_doc.GetElementById("detail-back"))
		, m_name(_doc.GetElementById("detail-name"))
		, m_description(_doc.GetElementById("detail-description"))
		, m_meta(_doc.GetElementById("detail-meta"))
		, m_status(_doc.GetElementById("detail-status"))
		, m_thumbnail(_doc.GetElementById("detail-thumbnail"))
		, m_thumbnailImage(m_thumbnail ? m_thumbnail->QuerySelector(".thumbnail-image") : nullptr)
		, m_formats(_doc.GetElementById("detail-formats"))
		, m_removeAll(_doc.GetElementById("detail-remove-all"))
		, m_data(_doc.GetElementById("detail-data"))
		, m_dataSize(_doc.GetElementById("detail-data-size"))
		, m_backupButton(_doc.GetElementById("detail-backup"))
		, m_dataNote(_doc.GetElementById("detail-data-note"))
		, m_backupList(_doc.GetElementById("detail-backup-list"))
		, m_fxToggle(_doc.GetElementById("detail-fx-toggle"))
	{
		if (m_removeAll)
			juceRmlUi::EventListener::Add(m_removeAll, Rml::EventId::Click, [this](Rml::Event&) { onRemoveAll(); });

		if (m_backupButton)
			juceRmlUi::EventListener::Add(m_backupButton, Rml::EventId::Click, [this](Rml::Event&) { onBackup(); });

		if (m_fxToggle)
		{
			juceRmlUi::EventListener::Add(m_fxToggle, Rml::EventId::Click, [this](Rml::Event&)
			{
				m_fxSelected = !m_fxSelected;
				m_fxToggle->SetClass("checked", wantsFx());

				// Every row says something different now: what its button offers, and what the trash
				// can beside it would take.
				refreshAll();
			});
		}
	}

	DetailPage::~DetailPage()
	{
		*m_alive = false;
	}

	void DetailPage::show(const Product& _product, PluginManager& _pluginManager)
	{
		if (!m_page || !m_formats)
			return;

		m_pluginManager = &_pluginManager;
		m_product = _product;
		m_productId = _product.product;

		// Nothing of the last product carries over: another plugin has another effect build, or
		// none, and an install queue from the page before is not about this one.
		m_pending.clear();
		m_fxProduct = _pluginManager.getCatalog().fxProductOf(_product.product);
		m_fxSelected = false;

		// One task per product shown, so an install started here is forgotten when the page moves on.
		m_installTask = std::make_unique<InstallTask>(_pluginManager, m_rmlComponent);

		const auto& catalog = _pluginManager.getCatalog();

		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();

		setText(m_name, _product.name.empty() ? _product.product : _product.name);
		setText(m_description, _product.description);
		setStatus({});

		if (m_meta)
		{
			auto meta = "Product: " + _product.product;

			// What ships under a name of its own and is not offered here: a test console, say. The
			// effect build is one of these too but has a chip and a column of its own now, so naming
			// it again here would only say the same thing twice.
			std::string others;

			for (const auto& variant : catalog.getVariants(_product.product))
			{
				if (variant == m_fxProduct)
					continue;

				others += (others.empty() ? "" : ", ") + variant;
			}

			if (!others.empty())
				meta += "  |  Also available: " + others;

			// Which of the protected channels are open here, since that is what decides whether the
			// versions below are all there are.
			std::string logins;

			if (_pluginManager.isLoggedIn(Channel::Internal, {}))
				logins = "team";

			if (_pluginManager.isLoggedIn(Channel::Donators, _product.slug))
				logins += (logins.empty() ? "" : ", ") + std::string("donators");

			meta += "  |  Logins: " + (logins.empty() ? std::string("none") : logins);

			setText(m_meta, meta);
		}

		m_thumbnailFilename = _product.thumbnail.empty()
			? std::string()
			: juce::URL(_product.thumbnail).getFileName().toStdString();

		updateThumbnail();

		setupFxToggle();

		m_rows.clear();

		// Every row but the template and the heading, which are the ones the document brings.
		for (int i = m_formats->GetNumChildren() - 1; i >= 0; --i)
		{
			auto* child = m_formats->GetChild(i);

			if (child->GetId() != "detail-row-template" && !child->IsClassSet("detail-head"))
				m_formats->RemoveChild(child);
		}

		for (const auto& format : catalog.getFormats(_product.product, os, arch))
		{
			const auto versions = catalog.getVersions(_product.product, format, os, arch);

			if (versions.empty())
				continue;

			FormatRow row;

			row.format = format;
			row.newest = versions.front();

			// The effect build is a product of its own, so it has its own idea of which formats it
			// comes in - and at the version this row would install.
			row.fxOffered = !m_fxProduct.empty() &&
				catalog.find(m_fxProduct, format, row.newest, os, arch) != nullptr;

			addFormatRow(std::move(row));
		}

		// The product's own folder, which has nothing to do with the formats above and everything
		// to do with what is lost if it goes.
		refreshBackups();
	}

	// Both the setup and the click: what the chip says and whether it is there at all depend on
	// the product, and ticking it changes what every row below says about itself.
	void DetailPage::setupFxToggle()
	{
		if (!m_fxToggle)
			return;

		// Named after the product it fetches rather than labelled "FX": this page is the one that
		// shows a plugin in full, and the archives hold the effect build under that name.
		setText(m_fxToggle, m_fxProduct.empty() ? std::string("FX") : m_fxProduct);

		m_fxToggle->SetClass("hidden", m_fxProduct.empty());
		m_fxToggle->SetClass("checked", wantsFx());

		if (m_formats)
			m_formats->SetClass("has-fx", !m_fxProduct.empty());
	}

	void DetailPage::addFormatRow(FormatRow _row)
	{
		auto* templateRow = m_doc.GetElementById("detail-row-template");

		if (!templateRow)
			return;

		auto* element = m_formats->AppendChild(m_doc.CreateElement("div"));

		if (!element)
			return;

		element->SetClassNames("detail-row");
		element->SetInnerRML(templateRow->GetInnerRML());

		_row.element = element;
		_row.path = dynamic_cast<Rml::ElementFormControlInput*>(element->QuerySelector(".detail-path"));
		_row.state = element->QuerySelector(".detail-state");
		_row.fxState = element->QuerySelector(".detail-fx-state");
		_row.installButton = element->QuerySelector(".detail-install");

		setText(element->QuerySelector(".detail-format"), _row.format);

		const auto& catalog = m_pluginManager->getCatalog();

		const auto channel = catalog.getChannel(m_product.product, _row.format, _row.newest,
			PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch());

		setText(element->QuerySelector(".detail-version"), _row.newest + channelSuffix(channel));

		const auto index = m_rows.size();

		if (_row.path)
		{
			// Where this product's build of this format goes, which may be a place of its own or
			// wherever the format goes for everything else. Emptying the field puts it back to that.
			juceRmlUi::EventListener::Add(_row.path, Rml::EventId::Change, [this, index](Rml::Event&)
			{
				auto& row = m_rows[index];

				if (row.path)
					setPath(row, row.path->GetValue());
			});
		}

		if (auto* browse = element->QuerySelector(".detail-browse"))
			juceRmlUi::EventListener::Add(browse, Rml::EventId::Click, [this, index](Rml::Event&) { onBrowse(index); });

		if (_row.installButton)
			juceRmlUi::EventListener::Add(_row.installButton, Rml::EventId::Click, [this, index](Rml::Event&) { onInstall(index); });

		if (auto* remove = element->QuerySelector(".detail-remove"))
			juceRmlUi::EventListener::Add(remove, Rml::EventId::Click, [this, index](Rml::Event&) { onRemove(index); });

		m_rows.push_back(std::move(_row));

		refreshRow(m_rows.back());
	}

	void DetailPage::refreshRow(FormatRow& _row) const
	{
		const auto dir = m_pluginManager->getInstallPath(m_product.product, _row.format);

		_row.installed = m_pluginManager->isInstalled(m_product.product, _row.format);
		_row.installedVersion = _row.installed
			? m_pluginManager->getInstalledVersion(m_product.product, _row.format)
			: std::string();

		_row.fxInstalled = false;
		_row.fxInstalledVersion.clear();

		if (!m_fxProduct.empty())
		{
			// The same folder as the plugin, which is what asking for the effect build's own location
			// answers - it has none of its own.
			_row.fxInstalled = m_pluginManager->isInstalled(m_fxProduct, _row.format);

			if (_row.fxInstalled)
				_row.fxInstalledVersion = m_pluginManager->getInstalledVersion(m_fxProduct, _row.format);
		}

		if (_row.path)
			_row.path->SetValue(dir);

		if (_row.state)
		{
			// An installed build that cannot say which version it is says so, rather than being left
			// looking uninstalled.
			setText(_row.state, _row.installed ? (_row.installedVersion.empty() ? "yes" : _row.installedVersion) : "no");
			_row.state->SetClass("yes", _row.installed);

			// Amber for a build the button beside it is offering to replace, which is the one colour
			// the list keeps for the same thing.
			_row.state->SetClass("outdated", _row.installed
				&& m_pluginManager->isOutdated(m_product.product, _row.format, _row.installedVersion, _row.newest));
		}

		if (_row.fxState)
		{
			// A format the effect build does not come in says so, which is a different answer from
			// one that it does come in and is not installed for.
			setText(_row.fxState, !_row.fxOffered && !_row.fxInstalled
				? "-"
				: (_row.fxInstalled ? (_row.fxInstalledVersion.empty() ? "yes" : _row.fxInstalledVersion) : "no"));

			_row.fxState->SetClass("yes", _row.fxInstalled);
			_row.fxState->SetClass("outdated", _row.fxInstalled
				&& m_pluginManager->isOutdated(m_fxProduct, _row.format, _row.fxInstalledVersion, _row.newest));
		}

		if (_row.installButton)
		{
			// The same rules the list goes by, in words rather than as an icon. Only the newest
			// version is offered here, so a downgrade is not one of the answers.
			const auto action = actionFor(_row);

			_row.installButton->SetInnerRML(action == InstallAction::Install ? "Install"
				: (action == InstallAction::Upgrade ? "Upgrade" : "Reinstall"));

			_row.installButton->SetClass("upgrade", action == InstallAction::Upgrade);

			// The same version again, which is neither of the other two answers and is coloured as
			// neither of them - see the stylesheet.
			_row.installButton->SetClass("reinstall", action == InstallAction::Reinstall);
		}

		// What the trash can acts on, which is both of them while the chip is ticked.
		if (_row.element)
			_row.element->SetClass("is-installed", _row.installed || (wantsFx() && _row.fxInstalled));
	}

	InstallAction DetailPage::actionFor(const FormatRow& _row) const
	{
		const auto plugin = m_pluginManager->installAction(m_product.product, _row.format, _row.installed,
			_row.installedVersion, _row.newest);

		if (!fxIncluded(_row))
			return plugin;

		return combineInstallActions(plugin, m_pluginManager->installAction(m_fxProduct, _row.format, _row.fxInstalled,
			_row.fxInstalledVersion, _row.newest));
	}

	void DetailPage::refreshAll()
	{
		for (auto& row : m_rows)
			refreshRow(row);

		// Filled while an effect build is on the disk, as in the list.
		if (m_fxToggle)
		{
			m_fxToggle->SetClass("installed", std::any_of(m_rows.begin(), m_rows.end(),
				[](const FormatRow& _row) { return _row.fxInstalled; }));
		}
	}

	void DetailPage::setPath(FormatRow& _row, const std::string& _path)
	{
		m_pluginManager->setInstallPath(m_product.product, _row.format, _path);

		refreshRow(_row);

		m_rmlComponent.enqueueUpdate();
	}

	void DetailPage::onBrowse(const size_t _index)
	{
		auto& row = m_rows[_index];

		const auto current = juce::File(juce::String(m_pluginManager->getInstallPath(m_product.product, row.format)));

		m_chooser = std::make_unique<juce::FileChooser>(
			"Where to install " + juce::String(m_product.product) + " " + juce::String(row.format), current);

		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
			[this, _index, alive = m_alive](const juce::FileChooser& _chooser)
			{
				if (!*alive)
					return;

				const auto result = _chooser.getResult();

				if (result == juce::File())
					return;

				// The browser answers outside of an RmlUi handler, so the DOM is taken here.
				juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);

				setPath(m_rows[_index], result.getFullPathName().toStdString());
			});
	}

	// The button in one row, whatever its word says: all three of them install the newest version of
	// the plugin and, where it is part of the row, of its effect build after it - each of them only if
	// the button is about it.
	void DetailPage::onInstall(const size_t _index)
	{
		if (!m_installTask || m_installTask->isRunning())
			return;

		m_pending.clear();

		const auto& row = m_rows[_index];
		const auto column = actionFor(row);

		// Only what the row's button is about - see fetchesPart.
		if (fetchesPart(m_pluginManager->installAction(m_product.product, row.format, row.installed,
			row.installedVersion, row.newest), column))
		{
			m_pending.push_back({_index, false});
		}

		// After the plugin rather than beside it: both go into the same folder, and one archive
		// unpacking over the other is how a half-written build ends up there.
		if (fxIncluded(row) && fetchesPart(m_pluginManager->installAction(m_fxProduct, row.format,
			row.fxInstalled, row.fxInstalledVersion, row.newest), column))
		{
			m_pending.push_back({_index, true});
		}

		installNext();
	}

	void DetailPage::installNext()
	{
		if (m_pending.empty() || !m_installTask)
			return;

		const auto job = m_pending.front();
		m_pending.erase(m_pending.begin());

		const auto& row = m_rows[job.format];
		const auto& product = job.fx ? m_fxProduct : m_product.product;

		const auto* asset = m_pluginManager->getCatalog().find(product, row.format, row.newest,
			PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch());

		if (!asset)
		{
			setStatus("No download for " + product + " " + row.format + " " + row.newest);
			installNext();
			return;
		}

		// Named, because the plugin and its effect build going by in one run are otherwise two
		// identical lines.
		const auto what = product + " " + row.format + " " + row.newest;

		setStatus("Downloading " + what + "...");

		m_installTask->start(*asset,
			job.fx ? m_fxProduct : (m_product.name.empty() ? m_product.product : m_product.name),
			[this, what](const float _progress)
			{
				if (_progress < 0.0f)
					return;

				setStatus("Downloading " + what + " " +
					std::to_string(static_cast<int>(_progress * 100.0f)) + "%");
			},
			[this, index = job.format](const bool _success, const std::string& _message)
			{
				setStatus(_success ? _message : "Failed: " + _message);

				refreshRow(m_rows[index]);

				// Stop the run rather than carrying on to the effect build: the reason is usually the
				// same for both of them - no write access, no network.
				if (!_success)
				{
					m_pending.clear();
					return;
				}

				installNext();
			});
	}

	void DetailPage::onRemove(const size_t _index)
	{
		auto& row = m_rows[_index];

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

		const auto results = m_pluginManager->uninstallMany(items, failure);
		const auto removed = std::find(results.begin(), results.end(), false) == results.end();
		const auto message = removed ? items.back().first + " " + row.format + " removed" : failure;

		setStatus(removed ? message : "Failed: " + message);

		refreshRow(row);

		if (removed)
			return;

		juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
			juce::String(m_product.product) + ": could not remove " + juce::String(row.format),
			juce::String(message));
	}

	// Everything that is installed, in one go - and asked about first, because it is the one button
	// here that throws several things away at once.
	namespace
	{
		// Round numbers rather than exact ones: this says whether a backup is instant or worth
		// waiting for, and nobody counts bytes to decide that.
		std::string readableSize(const juce::int64 _bytes)
		{
			if (_bytes >= 1024 * 1024)
				return std::to_string(_bytes / (1024 * 1024)) + " MB";

			if (_bytes >= 1024)
				return std::to_string(_bytes / 1024) + " KB";

			return std::to_string(_bytes) + " bytes";
		}
	}

	void DetailPage::refreshBackups()
	{
		if (!m_data)
			return;

		auto* templateRow = m_doc.GetElementById("detail-backup-template");

		if (!m_backupList)
			return;

		while (m_backupList->GetNumChildren())
			m_backupList->RemoveChild(m_backupList->GetChild(0));

		// Said in full, because the short label above cannot be both brief and honest. Patches are
		// the ones that catch people out: keeping them outside this folder is what we recommend, and
		// the patch manager writes its renames and categories as a .json beside each source file
		// rather than in here - so for a bank that lives elsewhere, neither is in a backup.
		setText(m_dataNote, "Everything in " + PlatformUtils::dataFolder(m_productId) +
			" - the settings, the skins, the ROMs and the patch manager's own database. Patches kept "
			"outside that folder are not in a backup, nor are the .json files the patch manager writes "
			"beside them to remember renames and categories.");

		const auto size = Backup::sizeOf(m_productId);

		setText(m_dataSize, size ? readableSize(size) : std::string("nothing yet"));

		if (m_backupButton)
			m_backupButton->SetClass("disabled", m_busy || size == 0);

		const auto backups = Backup::list(m_productId);

		if (backups.empty())
		{
			auto* empty = m_backupList->AppendChild(m_doc.CreateElement("div"));

			if (empty)
			{
				empty->SetClassNames("detail-empty");
				empty->SetInnerRML("No backups yet.");
			}

			return;
		}

		if (!templateRow)
			return;

		for (const auto& backup : backups)
		{
			auto* element = m_backupList->AppendChild(m_doc.CreateElement("div"));

			if (!element)
				continue;

			element->SetClassNames("detail-row");
			element->SetInnerRML(templateRow->GetInnerRML());

			setText(element->QuerySelector(".detail-backup-when"),
				backup.time.formatted("%Y-%m-%d %H:%M").toStdString());

			setText(element->QuerySelector(".detail-backup-size"), readableSize(backup.size));

			const auto file = backup.file;

			if (auto* reveal = element->QuerySelector(".detail-backup-reveal"))
			{
#if JUCE_MAC
				reveal->SetAttribute("title", "Show in Finder");
#elif JUCE_WINDOWS
				reveal->SetAttribute("title", "Show in Explorer");
#else
				reveal->SetAttribute("title", "Show in file manager");
#endif
				juceRmlUi::EventListener::Add(reveal, Rml::EventId::Click, [this, file](Rml::Event&) { onRevealBackup(file); });
			}

			if (auto* restore = element->QuerySelector(".detail-backup-restore"))
			{
				restore->SetClass("disabled", m_busy);
				juceRmlUi::EventListener::Add(restore, Rml::EventId::Click, [this, file](Rml::Event&) { onRestore(file); });
			}

			if (auto* del = element->QuerySelector(".detail-backup-delete"))
			{
				del->SetClass("disabled", m_busy);
				juceRmlUi::EventListener::Add(del, Rml::EventId::Click, [this, file](Rml::Event&) { onDeleteBackup(file); });
			}
		}
	}

	void DetailPage::onBackup()
	{
		if (m_busy)
			return;

		if (!Backup::sizeOf(m_productId))
		{
			setStatus(m_productId + " has nothing to back up yet");
			return;
		}

		m_busy = true;
		setStatus("Backing up " + m_productId + "...");
		refreshBackups();

		Backup::createAsync(m_productId, [this, alive = m_alive](const bool, const std::string& _message)
		{
			// The page may be showing another product by now, or be gone altogether.
			if (!*alive)
				return;

			juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);

			m_busy = false;
			setStatus(_message);
			refreshBackups();

			m_rmlComponent.enqueueUpdate();
		});
	}

	void DetailPage::onRestore(const std::string& _file)
	{
		const auto name = m_product.name.empty() ? m_product.product : m_product.name;

		// Said plainly, because both halves of it surprise somebody: what the backup holds comes
		// back, and what is there now is kept rather than quietly thrown away.
		confirm(m_rmlComponent, "Restore " + name + "?",
			"This puts everything from " +
			juce::File(juce::String(_file)).getFileName().toStdString() +
			" back, over what is there now.\n\nWhat is there now is backed up first, so this can be "
			"undone. Close the plugin everywhere before restoring - a host that has it open may write "
			"its own settings back out afterwards.",
			m_alive, [this, _file]
		{
			if (m_busy)
				return;

			m_busy = true;
			setStatus("Restoring...");
			refreshBackups();

			Backup::restoreAsync(_file, m_productId,
				[this, alive = m_alive](const float _progress)
				{
					if (!*alive)
						return;

					juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);

					setStatus("Restoring... " + std::to_string(static_cast<int>(_progress * 100.0f)) + "%");

					m_rmlComponent.enqueueUpdate();
				},
				[this, alive = m_alive](const bool, const std::string& _message)
				{
					if (!*alive)
						return;

					juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);

					m_busy = false;
					setStatus(_message);
					refreshBackups();

					m_rmlComponent.enqueueUpdate();
				});
		});
	}

	// Not what it holds - where it is. Somebody wanting to copy a backup onto a stick, or send it
	// to us, needs the folder rather than the file open.
	void DetailPage::onRevealBackup(const std::string& _file) const
	{
		juce::File(juce::String(_file)).revealToUser();
	}

	void DetailPage::onDeleteBackup(const std::string& _file)
	{
		if (m_busy)
			return;

		confirm(m_rmlComponent, "Delete this backup?",
			juce::File(juce::String(_file)).getFileName().toStdString() +
			" is deleted. Nothing that is installed changes.",
			m_alive, [this, _file]
		{
			std::string error;

			if (!Backup::remove(_file, error))
				setStatus(error);

			refreshBackups();
		});
	}

	void DetailPage::onRemoveAll()
	{
		std::vector<std::string> installed;
		std::vector<std::string> fxInstalled;

		for (const auto& row : m_rows)
		{
			if (row.installed)
				installed.push_back(row.format);

			if (wantsFx() && row.fxInstalled)
				fxInstalled.push_back(row.format);
		}

		if (installed.empty() && fxInstalled.empty())
		{
			setStatus("Nothing is installed");
			return;
		}

		const auto name = m_product.name.empty() ? m_product.product : m_product.name;

		// Asked about under the formats of both: a format the effect build is installed for is one
		// this is about to empty, whether or not the plugin itself is there too.
		auto asked = installed;

		for (const auto& format : fxInstalled)
		{
			if (std::find(asked.begin(), asked.end(), format) == asked.end())
				asked.push_back(format);
		}

		confirmUninstall(m_rmlComponent, name, asked, m_alive, [this, installed, fxInstalled]
		{
			// The effect builds first, for the same reason one row removes them first: what is left
			// behind when half of this fails should be the plugin rather than the thing beside it. All
			// in one call, so a system folder asks for administrator rights once (EMU-221).
			std::vector<std::pair<std::string, std::string>> items;

			for (const auto& format : fxInstalled)
				items.emplace_back(m_fxProduct, format);

			for (const auto& format : installed)
				items.emplace_back(m_product.product, format);

			// The first failure, and the effect builds ran first - so theirs is the one said when both
			// went wrong, as before.
			std::string message;

			const auto results = m_pluginManager->uninstallMany(items, message);
			const auto all = std::find(results.begin(), results.end(), false) == results.end();

			refreshAll();

			// The count only matters when nothing went wrong at all.
			if (message.empty())
				message = "Removed " + std::to_string(results.size()) + (results.size() == 1 ? " build" : " builds");

			setStatus(all ? message : "Failed: " + message);

			if (all)
				return;

			juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
				"Could not remove everything", juce::String(message));
		});
	}

	void DetailPage::setStatus(const std::string& _text) const
	{
		setText(m_status, _text);
	}

	void DetailPage::updateThumbnail()
	{
		if (!m_thumbnailImage)
			return;

		const auto hasImage = !m_thumbnailFilename.empty() && m_dataProvider.hasImage(m_thumbnailFilename);

		// Set either way. The page is reused for every product, so one without a picture of its own
		// would otherwise keep showing the picture of the last one that had.
		m_thumbnailImage->SetAttribute("src", hasImage ? m_thumbnailFilename : std::string("placeholder.png"));
	}
}
