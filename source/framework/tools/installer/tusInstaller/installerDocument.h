#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <set>
#include <string>
#include <vector>

#include "loginDialog.h"
#include "pluginEntryController.h"

#include "pluginManagerLib/pluginManager.h"

namespace Rml
{
	class Element;
	class ElementDocument;
}

namespace juceRmlUi
{
	class RmlComponent;
	struct RmlInterfaces;
	class Tooltips;
}

namespace pluginManager
{
	class ChangelogPage;
	class DetailPage;
	class InstallerDataProvider;
	class PreferencesPage;

	// Owns the RmlUi document and turns the catalog into one row per product.
	//
	// Every public method takes RmlUi access itself, so none of them may be called from inside an RmlUi
	// event handler. The document's own handlers post what they report instead.
	class InstallerDocument
	{
	public:
		using LoginRequest = std::function<void(const Product& _product)>;

		// The list, one product in detail, and the install locations take turns in the window.
		enum class Page
		{
			List,
			Detail,
			Changelog,
			Preferences
		};

		explicit InstallerDocument(InstallerDataProvider& _dataProvider);
		~InstallerDocument();

		juceRmlUi::RmlComponent* getRmlComponent() const { return m_rmlComponent.get(); }

		// The team login, the donator login - which is global now, since a plugin that is not
		// published is only known by its folder - and what to do when the catalog has to be read
		// again, which removing a login makes necessary.
		void setHandlers(std::function<void()> _onTeamLogin, std::function<void()> _onDonatorLogin,
		                 std::function<void()> _onRefresh);

		// Not const: the preferences page writes the install locations back.
		void rebuild(PluginManager& _pluginManager);

		void setStatus(const std::string& _text);

		// What the last refresh could not do, or nothing when it did all of it. Kept rather than
		// only shown: the list is built again whenever an install location changes, and the status
		// line it writes then would otherwise be a plugin count that looks like all is well.
		void setProblem(std::string _problem);

		// Shown while the catalog is being fetched, which is otherwise a silent few seconds.
		void setBusy(bool _busy);
		void setTeamLoggedIn(bool _loggedIn);

		// Off unless the settings file says otherwise, and then the button is not in the window at
		// all rather than disabled: there is nothing to explain to somebody it is not meant for.
		void setTeamLoginAvailable(bool _available);
		void onThumbnailReady(const std::string& _filename);

		// Offers the newer build of this program, when the catalog holds one, and fetches it over
		// the top of this one. See SelfUpdate for why that is not simply an install.
		void updateSelfUpdateButton(const Catalog& _catalog);
		void startSelfUpdate();

		void openLogin(const std::string& _title, bool _withFolder, LoginDialog::SubmitCallback _onSubmit);
		void loginFailed(const std::string& _message);
		void closeLogin();

	private:
		// What the user has ticked. The rows own this while they exist, and every one of them is
		// destroyed each time the catalog is read - so without a copy out here a refresh, or a walk
		// into Preferences and back, silently forgets it.
		//
		// What is wanted rather than what is not, so that a product or a format seen for the first
		// time arrives unticked (EMU-248). Everything ticked was what a first start showed, and a
		// first "Install selected" then put every format of every product on the machine.
		struct Selection
		{
			std::set<std::string> selectedFormats;
			bool fx = false;
		};

		Rml::ElementDocument* getDocument() const;

		// Runs DOM work with RmlUi access and asks for a redraw. Does nothing before the document exists.
		void withDocument(const std::function<void(Rml::ElementDocument&)>& _func);
		void bindDocument(Rml::ElementDocument& _doc);

		// Every format any of the products offers here. The toggles are built from it, and so is the
		// column each row lays out per format.
		static std::vector<std::string> collectFormats(const Catalog& _catalog);

		// One chip per format any of the products offers here, each ticking that format everywhere.
		void buildFormatToggles(Rml::ElementDocument& _doc, const std::vector<std::string>& _formats);
		void setFormatSelectedEverywhere(const std::string& _format, bool _selected);

		// Install selected and Upgrade all: the same thing every row's All column does, over every
		// row. What they say they would do is counted from the rows themselves.
		void updateBulkButtons();
		void startBulk(PluginEntryController::Bulk _what);

		// One row at a time, so that six products do not download at once and ask for administrator
		// rights six times over.
		void bulkNext();

		// The end of a walk: one request for administrator rights for everything it unpacked, and
		// then every row says what is on disk.
		void finishBulkElevated();

		void createPreferencesPage(Rml::ElementDocument& _doc, PluginManager& _pluginManager);

		// The way out of Preferences, whether by its Back button or by pressing Preferences again.
		void closePreferences();

		void showPage(Page _page);

		// The changelog of the product the detail page shows.
		void showChangelog();
		void showDetail(const Product& _product);

		void setStatusInternal(Rml::ElementDocument& _doc, const std::string& _text);

		// Label, colour and whether the section is there at all, from the two flags below.
		void updateTeamLogin() const;

		// Read off the rows before they are thrown away, put back on the ones built in their place.
		void rememberSelection();

		// And keeps it between runs, because somebody who installs one format only has said so
		// already and should not have to say it again every time the window opens.
		//
		// What is written down is what was turned OFF. A format that turns up later - a plugin that
		// gains one, or a Mac, which has AU where the others do not - is then ticked like everything
		// else, rather than off for never having been in the file.
		void loadSelection();
		void saveSelection() const;
		void applySelection(PluginEntryController& _entry) const;
		bool isFormatSelectedAnywhere(const std::string& _format) const;

		// Select all and Deselect all are the same button; what it says is what pressing it does.
		bool everythingSelected() const;
		void updateSelectAll() const;

		// The chips read the rows, so anything that moves every row at once has to refresh them.
		void updateFormatChips() const;

		// What every row says is installed, read from the disk again. Cheap - it is the manifest and
		// a look at whether the files are still there - and not a catalog refresh, which goes to the
		// network and is not what changed.
		void refreshInstalled();

		// The status line under the header: how much is on offer, and why that is not everything.
		std::string summary(size_t _shown) const;

		// Right-click, which RmlUi has no event of its own for. On a row it offers to hide that row,
		// anywhere in the list it offers to bring back what is hidden.
		void openListMenu(Rml::Event& _event);

		void createEntry(const Product& _product, PluginManager& _pluginManager,
		                 Rml::Element& _container, Rml::ElementDocument& _doc,
		                 const std::vector<std::string>& _formats);
		void downloadThumbnails(const Catalog& _catalog);

		InstallerDataProvider& m_dataProvider;
		std::unique_ptr<juceRmlUi::RmlInterfaces> m_rmlInterfaces;
		std::unique_ptr<juceRmlUi::RmlComponent> m_rmlComponent;

		std::vector<std::unique_ptr<PluginEntryController>> m_entries;

		// What is left of a run started above the list, and what that run is doing. The entries are
		// owned above; a rebuild empties this with them.
		std::vector<PluginEntryController*> m_bulkQueue;

		// Every copy the whole walk could not make itself, asked for once when it is through rather
		// than once per product - which is four prompts a plugin on a machine that keeps its plugins
		// in a system folder.
		PluginInstaller::Pending m_bulkElevated;
		PluginEntryController::Bulk m_bulkWhat = PluginEntryController::Bulk::Install;

		std::string m_problem;

		// The header's Refresh, and whether there is a catalog on its way already - a second one
		// started on top of the first would be twenty requests for an answer nobody reads.
		Rml::Element* m_refreshButton = nullptr;
		Rml::Element* m_selfUpdate = nullptr;

		// The pictures being fetched right now, by file name. Several products share one - a test
		// console is shown with the picture of the plugin it tests - and each of them asking for it
		// is what has to be stopped. Emptied as each answer comes back, so a Refresh retries one
		// that failed.
		std::set<std::string> m_thumbnailsWanted;

		// Says this is still here when an answer arrives from a thread that was started before it
		// went away.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

		// Outlives the call that starts it, because the install runs on a worker.
		PluginInstaller m_selfUpdater;
		bool m_busy = false;

		Rml::Element* m_installSelected = nullptr;
		Rml::Element* m_upgradeAll = nullptr;

		std::unique_ptr<LoginDialog> m_login;
		std::unique_ptr<DetailPage> m_detail;
		std::unique_ptr<ChangelogPage> m_changelog;
		std::unique_ptr<PreferencesPage> m_preferences;
		std::unique_ptr<juceRmlUi::Tooltips> m_tooltips;
		Rml::Element* m_pluginList = nullptr;
		Rml::Element* m_formatToggles = nullptr;
		Rml::Element* m_selectAll = nullptr;
		Rml::Element* m_teamLogin = nullptr;
		Rml::Element* m_teamLoginSection = nullptr;

		// Rows the settings leave out of the list, counted while it is built so that the status
		// line can say how many are missing and why.
		size_t m_hiddenCount = 0;

		std::map<std::string, Selection> m_selection;		// by product

		// Kept from the last rebuild, so that a page opened later works from the same catalog and
		// the same settings as the list does.
		PluginManager* m_pluginManager = nullptr;

		Page m_page = Page::List;

		// Kept so the button reads right even when the document is bound after the login state is known.
		bool m_teamLoggedIn = false;
		bool m_teamLoginAvailable = false;

		std::function<void()> m_onTeamLogin;
		std::function<void()> m_onDonatorLogin;
		std::function<void()> m_onRefresh;
	};
}
