#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "pluginManagerLib/pluginManager.h"

namespace juce { class FileChooser; }

namespace Rml
{
	class Element;
	class ElementDocument;
	class ElementFormControlInput;
}

namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	// Where each format is installed - one row per format, with a file browser and a way back to the
	// platform's default - where backups go, and which donator logins are stored, since a plugin that is not published
	// is known by nothing but the folder one of those opens.
	//
	// Also what changing a location left behind, because this is the page that invites the change:
	// see PluginManager::getStrayInstalls.
	//
	// Edits are kept as they are typed and written out when the field is left or the page is closed,
	// rather than once per keystroke.
	class PreferencesPage
	{
	public:
		PreferencesPage(Rml::ElementDocument& _doc, PluginManager& _pluginManager, juceRmlUi::RmlComponent& _rmlComponent);
		~PreferencesPage();

		// Adding a login opens a prompt the app owns; removing one means the catalog has to be read
		// again, which the app does too.
		void setHandlers(std::function<void()> _onAddDonatorLogin, std::function<void()> _onRefresh);

		Rml::Element* getBackButton() const { return m_back; }
		Rml::Element* getPage() const { return m_page; }

		// Called every time the page is opened: a location may have been changed elsewhere.
		void refresh();

		void save() const { m_pluginManager.getInstallPaths().save(); }

	private:
		struct Row
		{
			PluginFormat format = PluginFormat::Count;
			Rml::ElementFormControlInput* path = nullptr;
			Rml::Element* note = nullptr;
		};

		// PluginFormat::Count is the backup folder, which is a row like the others in every other way.
		void addRow(PluginFormat _format, Rml::Element& _parent);
		std::string pathOf(const Row& _row) const;
		bool isCustom(const Row& _row) const;
		void storePath(const Row& _row, const std::string& _path) const;
		void buildDonatorLogins();
		void buildStrayInstalls();
		void onRemoveStrayInstall(const InstalledPlugins::Entry& _entry);
		void onRemoveDonatorLogin(const std::string& _slug);
		void setPath(size_t _index, const std::string& _path);
		void updateNote(const Row& _row) const;
		void browse(size_t _index);

		Rml::ElementDocument& m_doc;
		PluginManager& m_pluginManager;
		juceRmlUi::RmlComponent& m_rmlComponent;

		Rml::Element* m_page = nullptr;
		Rml::Element* m_back = nullptr;
		Rml::Element* m_list = nullptr;
		Rml::Element* m_donatorLogins = nullptr;
		Rml::Element* m_strayInstalls = nullptr;
		Rml::Element* m_strayInstallList = nullptr;

		std::function<void()> m_onAddDonatorLogin;
		std::function<void()> m_onRefresh;

		std::vector<Row> m_rows;

		std::unique_ptr<juce::FileChooser> m_chooser;

		// The browser answers after this page may be gone, so what it reports is dropped by then.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
