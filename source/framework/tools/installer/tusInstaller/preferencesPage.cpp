#include "preferencesPage.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

#include <JuceHeader.h>

namespace pluginManager
{
	PreferencesPage::PreferencesPage(Rml::ElementDocument& _doc, PluginManager& _pluginManager,
	                                 juceRmlUi::RmlComponent& _rmlComponent)
		: m_doc(_doc)
		, m_pluginManager(_pluginManager)
		, m_rmlComponent(_rmlComponent)
		, m_page(_doc.GetElementById("preferences-page"))
		, m_back(_doc.GetElementById("preferences-back"))
		, m_list(_doc.GetElementById("preference-list"))
		, m_donatorLogins(_doc.GetElementById("donator-login-list"))
		, m_strayInstalls(_doc.GetElementById("stray-installs"))
		, m_strayInstallList(_doc.GetElementById("stray-install-list"))
	{
		if (auto* add = _doc.GetElementById("donator-login-add"))
		{
			juceRmlUi::EventListener::Add(add, Rml::EventId::Click, [this](Rml::Event&)
			{
				if (m_onAddDonatorLogin)
					m_onAddDonatorLogin();
			});
		}

		if (!m_list)
			return;

		for (const auto format : InstallPaths::getFormats())
			addRow(format);
	}

	void PreferencesPage::setHandlers(std::function<void()> _onAddDonatorLogin, std::function<void()> _onRefresh)
	{
		m_onAddDonatorLogin = std::move(_onAddDonatorLogin);
		m_onRefresh = std::move(_onRefresh);
	}

	PreferencesPage::~PreferencesPage()
	{
		*m_alive = false;
	}

	void PreferencesPage::addRow(const PluginFormat _format)
	{
		auto* templateRow = m_doc.GetElementById("preference-row-template");

		if (!templateRow)
			return;

		auto* element = m_list->AppendChild(m_doc.CreateElement("div"));

		if (!element)
			return;

		element->SetClassNames("preference-row");
		element->SetInnerRML(templateRow->GetInnerRML());

		if (auto* name = element->QuerySelector(".preference-format"))
			name->SetInnerRML(Rml::StringUtilities::EncodeRml(PlatformUtils::formatToString(_format)));

		Row row;
		row.format = _format;
		row.path = dynamic_cast<Rml::ElementFormControlInput*>(element->QuerySelector(".preference-path"));
		row.note = element->QuerySelector(".preference-note");

		const auto index = m_rows.size();

		if (row.path)
		{
			// Kept in memory as it is typed; the write-out happens when the field is left, which is
			// also what a click on Browse, Default or Back does.
			juceRmlUi::EventListener::Add(row.path, Rml::EventId::Change, [this, index](Rml::Event&)
			{
				auto& r = m_rows[index];
				m_pluginManager.getInstallPaths().set(r.format, r.path->GetValue());
				updateNote(r);
				buildStrayInstalls();
			});

			juceRmlUi::EventListener::Add(row.path, Rml::EventId::Blur, [this](Rml::Event&) { save(); });
		}

		if (auto* browseButton = element->QuerySelector(".preference-browse"))
			juceRmlUi::EventListener::Add(browseButton, Rml::EventId::Click, [this, index](Rml::Event&) { browse(index); });

		if (auto* defaultButton = element->QuerySelector(".preference-default"))
		{
			juceRmlUi::EventListener::Add(defaultButton, Rml::EventId::Click, [this, index](Rml::Event&)
			{
				setPath(index, InstallPaths::getDefault(m_rows[index].format));
				save();
			});
		}

		m_rows.push_back(row);
	}

	void PreferencesPage::refresh()
	{
		for (auto& row : m_rows)
		{
			if (row.path)
				row.path->SetValue(m_pluginManager.getInstallPaths().get(row.format));

			updateNote(row);
		}

		buildDonatorLogins();
		buildStrayInstalls();
	}

	// Rebuilt whenever a location changes as well as when the page opens, so that pointing a format
	// somewhere else says what it left behind while the field that did it is still on screen.
	void PreferencesPage::buildStrayInstalls()
	{
		if (!m_strayInstallList || !m_strayInstalls)
			return;

		auto* templateRow = m_doc.GetElementById("stray-install-template");

		if (!templateRow)
			return;

		for (int i = m_strayInstallList->GetNumChildren() - 1; i >= 0; --i)
		{
			auto* child = m_strayInstallList->GetChild(i);

			if (child->GetId() != "stray-install-template")
				m_strayInstallList->RemoveChild(child);
		}

		const auto strays = m_pluginManager.getStrayInstalls();

		m_strayInstalls->SetClass("hidden", strays.empty());

		for (const auto& stray : strays)
		{
			auto* element = m_strayInstallList->AppendChild(m_doc.CreateElement("div"));

			if (!element)
				continue;

			element->SetClassNames("stray-install-row");
			element->SetInnerRML(templateRow->GetInnerRML());

			if (auto* name = element->QuerySelector(".stray-install-name"))
				name->SetInnerRML(Rml::StringUtilities::EncodeRml(stray.product + " " + stray.format));

			// The folder rather than the file: it is where to go and look, and it is what the field
			// above used to say.
			if (auto* path = element->QuerySelector(".stray-install-path"))
			{
				const auto dir = juce::File(juce::String(stray.path)).getParentDirectory().getFullPathName();
				path->SetInnerRML(Rml::StringUtilities::EncodeRml(dir.toStdString()));
			}

			if (auto* remove = element->QuerySelector(".stray-install-remove"))
				juceRmlUi::EventListener::Add(remove, Rml::EventId::Click, [this, stray](Rml::Event&) { onRemoveStrayInstall(stray); });
		}
	}

	void PreferencesPage::onRemoveStrayInstall(const InstalledPlugins::Entry& _entry)
	{
		std::string message;

		const auto removed = m_pluginManager.removeStray(_entry, message);

		// The row going is what says it worked. A host holding the file open is the usual reason it
		// does not, and that needs saying.
		buildStrayInstalls();

		m_rmlComponent.enqueueUpdate();

		if (removed)
			return;

		juce::NativeMessageBox::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
			juce::String(_entry.product) + ": could not remove " + juce::String(_entry.format),
			juce::String(message));
	}

	// Built fresh every time the page is opened: a login can be added from the header as well.
	void PreferencesPage::buildDonatorLogins()
	{
		if (!m_donatorLogins)
			return;

		auto* templateRow = m_doc.GetElementById("donator-login-template");

		if (!templateRow)
			return;

		for (int i = m_donatorLogins->GetNumChildren() - 1; i >= 0; --i)
		{
			auto* child = m_donatorLogins->GetChild(i);

			if (child->GetId() != "donator-login-template")
				m_donatorLogins->RemoveChild(child);
		}

		for (const auto& slug : m_pluginManager.getDonatorLogins())
		{
			auto* element = m_donatorLogins->AppendChild(m_doc.CreateElement("div"));

			if (!element)
				continue;

			element->SetClassNames("donator-login-row");
			element->SetInnerRML(templateRow->GetInnerRML());

			if (auto* name = element->QuerySelector(".donator-login-name"))
				name->SetInnerRML(Rml::StringUtilities::EncodeRml(slug));

			if (auto* remove = element->QuerySelector(".donator-login-remove"))
				juceRmlUi::EventListener::Add(remove, Rml::EventId::Click, [this, slug](Rml::Event&) { onRemoveDonatorLogin(slug); });
		}
	}

	void PreferencesPage::onRemoveDonatorLogin(const std::string& _slug)
	{
		m_pluginManager.logout(Channel::Donators, _slug);

		buildDonatorLogins();

		m_rmlComponent.enqueueUpdate();

		// What that login brought in goes with it, so the catalog is read again.
		if (m_onRefresh)
			m_onRefresh();
	}

	void PreferencesPage::setPath(const size_t _index, const std::string& _path)
	{
		auto& row = m_rows[_index];

		m_pluginManager.getInstallPaths().set(row.format, _path);

		if (row.path)
			row.path->SetValue(m_pluginManager.getInstallPaths().get(row.format));

		updateNote(row);
		buildStrayInstalls();
	}

	void PreferencesPage::updateNote(const Row& _row) const
	{
		if (!_row.note)
			return;

		const auto path = m_pluginManager.getInstallPaths().get(_row.format);

		// Every format has somewhere to go now, so an empty path is one somebody emptied on purpose.
		const char* note = path.empty()
			? "not installed until a location is set"
			: (juce::File(juce::String(path)).isDirectory()
				? (m_pluginManager.getInstallPaths().isCustom(_row.format) ? "" : "default")
				: "will be created");

		_row.note->SetInnerRML(note);
	}

	void PreferencesPage::browse(const size_t _index)
	{
		auto& row = m_rows[_index];

		const auto current = juce::File(juce::String(m_pluginManager.getInstallPaths().get(row.format)));

		m_chooser = std::make_unique<juce::FileChooser>(
			"Install location for " + juce::String(PlatformUtils::formatToString(row.format)), current);

		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
			[this, _index, alive = m_alive](const juce::FileChooser& _chooser)
			{
				if (!*alive)
					return;

				const auto result = _chooser.getResult();

				if (result == juce::File())
					return;

				// The browser answers outside of an RmlUi handler, so the DOM is taken here.
				{
					juceRmlUi::RmlInterfaces::ScopedAccess sa(m_rmlComponent);
					setPath(_index, result.getFullPathName().toStdString());
					save();
				}

				m_rmlComponent.enqueueUpdate();
			});
	}
}
