#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "catalog.h"

#include "changelogLib/changelog.h"
#include "elevatedBatch.h"
#include "credentials.h"
#include "installPaths.h"
#include "installedPlugins.h"

namespace pluginManager
{
	// What both programs drive: the catalog of everything installable here, and the logins that decide
	// how much of it is visible.
	//
	// Everything blocks, so the library works without a message loop - which is what lets a script and
	// the tests use it. The *Async variants exist for the GUI: they run the same call on a worker
	// thread and answer on the JUCE message thread, and must be called from that thread.
	class PluginManager
	{
	public:
		using ResultCallback = std::function<void(bool _success, const std::string& _error)>;

		// _loadSettings is off for a test, which must not read or write the logins and install
		// locations of the machine it runs on.
		explicit PluginManager(bool _loadSettings = true);
		~PluginManager();

		// Replaces the catalog with what every channel the stored logins open has to offer. A channel
		// that cannot be read is skipped rather than failing the whole refresh - the public list is
		// worth showing even when a protected one is unreachable - and _error names the first failure.
		bool refresh(std::string& _error);

		// Only the newest request is answered, so a refresh started by a login supersedes one that is
		// still running instead of merging with it.
		void refreshAsync(ResultCallback _callback);

		const Catalog& getCatalog() const { return m_catalog; }

		// A product's changelog: from the newest place this user can read one - the product's internal
		// folder, then its donator folder - and else, for a product that was published, the changelog
		// of the public repository. That one ends at the newest release, unless the team login is used:
		// the team sees what is coming, everybody else what they can install.
		bool fetchChangelog(const std::string& _product, changelogLib::Changelog& _changelog, std::string& _error) const;

		using ChangelogCallback = std::function<void(bool _success, const changelogLib::Changelog& _changelog, const std::string& _error)>;
		void fetchChangelogAsync(const std::string& _product, ChangelogCallback _callback) const;

		bool isLoggedIn(const Channel _channel, const std::string& _slug) const { return m_credentials.has(_channel, _slug); }

		// The team login is only offered where the settings say so - see InstallPaths::hasTeamLogin.
		bool isTeamLoginEnabled() const { return m_installPaths.hasTeamLogin(); }

		// The folders a donator login is stored for. A product that is not published is in no product
		// list, so its folder is the only thing this program knows it by.
		std::vector<std::string> getDonatorLogins() const { return m_credentials.getSlugs(Channel::Donators); }

		// A login this machine already has, to hand to somebody else. No more readable than the
		// settings file it comes out of, which is plain text and says so.
		Credentials getLogin(const Channel _channel, const std::string& _slug) const
		{
			return m_credentials.get(_channel, _slug);
		}

		// Checked against the server before it is stored, so a wrong password is never remembered.
		bool login(Channel _channel, const std::string& _slug, const Credentials& _credentials, std::string& _error);
		void loginAsync(Channel _channel, const std::string& _slug, const Credentials& _credentials, ResultCallback _callback);
		void logout(Channel _channel, const std::string& _slug);

		// A login for this run only, not written to disk. What the headless program does with a login
		// handed to it on the command line.
		void setCredentials(Channel _channel, const std::string& _slug, const Credentials& _credentials);

		// The login a download of this build needs, empty for a public one.
		Credentials credentialsFor(const Asset& _asset) const;

		// For a thumbnail, which may sit beside the builds it belongs to and then needs the same login
		// they do. Empty for anything not on our own build server.
		Credentials credentialsForThumbnail(const Product& _product) const;

		// The files a product brings besides its builds - see FileSection - fetched into its data
		// folder after a build of it was installed. Which ones is said by the products.json of the
		// folder the build came from, for a build from a protected one, else by the product list.
		//
		// A file that is there already is only fetched again when the server's copy is newer than
		// it, so whatever the user changed in it since is kept. Nothing is ever removed. _message
		// says how many arrived, empty when none did, or what went wrong first.
		bool installAdditionalFiles(const Asset& _asset, std::string& _message) const;
		void installAdditionalFilesAsync(const Asset& _asset, ResultCallback _callback) const;

		// Where this build is installed. Empty means the format has no location, which is what stops
		// an install before it starts.
		std::string installPathFor(const Asset& _asset) const;

		// Where one product's format goes, and where to point it instead. An empty path puts the
		// product back to wherever the format goes for everything else. An effect build has no
		// location of its own - see owningProduct - so both of these answer about its plugin.
		std::string getInstallPath(const std::string& _product, const std::string& _format) const;
		void setInstallPath(const std::string& _product, const std::string& _format, const std::string& _path);

		// Removes what is installed for this product and format. _message says what happened either
		// way, and is what the user is shown.
		bool uninstall(const std::string& _product, const std::string& _format, std::string& _message);

		// Several of them in one go, which both the list and the detail page offer. Returns how many
		// were removed; _message is the first failure, or what was done when there was none.
		size_t uninstallAll(const std::string& _product, const std::vector<std::string>& _formats,
		                    std::string& _message);

		// Every product and format listed, behind at most one administrator prompt. One answer per
		// item, in order; _failure is the first thing that went wrong and stays empty when nothing did.
		//
		// For an action that takes more than one product out at once - a plugin and its effect build,
		// which is what removing a row does. Each call of uninstall() is a prompt of its own.
		std::vector<bool> uninstallMany(const std::vector<std::pair<std::string, std::string>>& _items,
		                                std::string& _failure);

		// What this program installed that is no longer where its format goes. Pointing a format
		// somewhere else leaves the copy that was there on disk, where every host that scans that
		// folder still loads it - and nothing else here would mention it again, because a version is
		// only reported for a record that matches the location as well.
		std::vector<InstalledPlugins::Entry> getStrayInstalls() const;

		// Removes one of those, from where it actually is rather than from where its format points
		// now. _message says what happened either way.
		bool removeStray(const InstalledPlugins::Entry& _entry, std::string& _message);

		InstallPaths& getInstallPaths() { return m_installPaths; }
		const InstallPaths& getInstallPaths() const { return m_installPaths; }

		// The folder a copy of this product and format is in, empty when there is none.
		//
		// Where it would be installed first, then the standard locations for the format - see
		// PlatformUtils::getSearchPaths. A build this program never installed counts: somebody who
		// unpacked an archive by hand, or installed before this program existed, or has the plugins
		// in the system folder on a Mac rather than their own, is offered an upgrade of what they
		// have rather than a download of something they already run.
		//
		// Asked partly of what was written down rather than only of the file system, because an
		// install is free to name its files: a program keeps the name it was built under, so looking
		// for a file named after the product would never find one. See InstalledPlugins::installedFile.
		std::string locateInstalled(const std::string& _product, PluginFormat _format) const;

		// Whether this product and format are installed anywhere this knows to look.
		bool isInstalled(const std::string& _product, const std::string& _format) const;

		// The version of this product that is installed for this format, empty when none is or it
		// cannot be told.
		std::string getInstalledVersion(const std::string& _product, const std::string& _format) const;

		// Whether this build is a different one from what is installed, for a channel that ships the
		// next build under the number the last one had. Two questions on purpose:
		//
		// isNewerBuild costs nothing - the date came with the listing the catalog was built from, so
		// every row can ask it on every refresh.
		//
		// confirmNewerBuild asks the server, and is for the moment before something is offered rather
		// than for a row that is only being drawn. It answers yes when it cannot tell: a build is not
		// worth withholding because a header was missing.
		// The build that would replace this program, or nothing. Worked out when the catalog is
		// fetched, because working it out can mean asking the server - which is fine on the thread a
		// refresh already runs on, and is not fine while a window is being drawn.
		const Asset* getSelfUpdate() const { return m_selfUpdate ? &*m_selfUpdate : nullptr; }

		// After this program has replaced itself: what it is now, so the next refresh has something
		// to compare against.
		void recordSelfUpdate(const Asset& _asset);

		bool isNewerBuild(const Asset& _asset) const;

		// Whether what is installed of a product in a format is behind _offered: a smaller version
		// number, or the same number from an older build - a protected channel ships every build under
		// one number. A copy that cannot say which version it is - a file that carries none, with
		// nothing written down about it - is behind when the file is clearly older than that build.
		bool isOutdated(const std::string& _product, const std::string& _format, const std::string& _installedVersion,
		                const std::string& _offered) const;

		// Whether anything of a product that is installed here - the plugin or its effect build, in any
		// format - is behind the newest build on offer for it. What the "Updates available" group at
		// the top of the list is made of (EMU-250).
		bool hasUpdate(const std::string& _product) const;

		// What installing _offered over what is installed does: decideInstallAction, except that an
		// install that is outdated - see above - is an upgrade even when the numbers say reinstall.
		InstallAction installAction(const std::string& _product, const std::string& _format, bool _installed,
		                            const std::string& _installedVersion, const std::string& _offered) const;
		bool confirmNewerBuild(const Asset& _asset) const;

		// The same question for a row, which knows a product, a format and a version rather than an
		// asset. False when this platform has no such build.
		bool hasNewerBuild(const std::string& _product, const std::string& _format,
		                   const std::string& _version) const;

		// Called after an install so that its version is known later on.
		// _files are the names the archive left in the install folder, from
		// PluginInstaller::lastInstalledFiles(). Without them an uninstall can only take the plugin.
		void recordInstall(const Asset& _asset, std::vector<std::string> _files = {});

		// The copies a run of installs left waiting, made now, behind ONE request for administrator
		// rights - which is the whole point of collecting them. What arrives is written down here,
		// because an install counts as installed only once its files are where they belong.
		//
		// True when every one of them landed. The batch is emptied either way: what failed is in
		// _error, and a half-finished run is not quietly retried behind the user's back.
		bool finishPending(ElevatedBatch& _batch, std::string& _error);

		// The product a build belongs to, which is the one whose location and login apply to it.
		std::string owningProduct(const std::string& _product) const;

	private:
		// Where an install of this product's format goes, asked for under the plugin an effect build
		// belongs to rather than under its own name: an install already puts it there, and looking
		// for it afterwards has to agree or it is looked for in a folder nothing ever put it in.
		//
		// The folder a copy was found in when that is not the one the settings name, so that an
		// upgrade lands on top of the build it is an upgrade of rather than beside it - two copies in
		// two folders a host scans leaves the user with both and no say in which one loads.
		std::string pathFor(const std::string& _product, PluginFormat _format) const;

		// Only what the settings say, which is where an install goes when nothing is installed yet.
		std::string configuredPathFor(const std::string& _product, PluginFormat _format) const;

		// One product and format to take out, and the folder to take it from: where the format points
		// for one that is installed, where the record says for one that was left behind.
		struct Removal
		{
			std::string product;
			PluginFormat format = PluginFormat::Count;
			std::string dir;
		};

		Removal removalFor(const std::string& _product, PluginFormat _format) const;

		// The body of every uninstall.
		//
		// What only an administrator may remove is collected across all of them and removed in one go
		// at the end, rather than asked about file by file: taking a product out of a system folder in
		// four formats, each with a changelog beside it, used to be eight prompts (EMU-221). Whether
		// each one went is then read from the disk, not from what was asked - a plugin a host has open
		// is the usual one that did not, and that is said for that plugin rather than for the run.
		std::vector<bool> removeAll(const std::vector<Removal>& _removals, std::string& _failure);

		// What is left to do once a plugin is gone: its record, and the folder made for one program.
		void forget(const std::string& _product, PluginFormat _format, const std::string& _dir);

		static bool fetchAll(Catalog& _catalog, const CredentialStore& _credentials, std::string& _error);

		// Everything fetchChangelog needs, gathered on the message thread so that the worker reads
		// nothing that belongs to it.
		struct ChangelogRequest
		{
			std::string name;			// for the message when there is none
			std::string slug;
			Credentials internal;
			Credentials donators;
			bool published = false;
			std::string newestPublished;	// where the public one ends, empty for all of it
		};

		ChangelogRequest changelogRequestFor(const std::string& _product) const;

		// Everything installAdditionalFiles needs, gathered on the message thread for the same reason.
		struct FilesRequest
		{
			std::string product;				// whose data folder the files go into
			std::vector<FileSection> files;		// from the build's folder, else from the product list
			Credentials internal;				// for files on our own build server - see loginForUrl
			Credentials donators;
		};

		FilesRequest filesRequestFor(const Asset& _asset) const;
		static bool installAdditionalFiles(const FilesRequest& _request, std::string& _message);

		// The login a file on our own build server needs, by the folder it is in rather than by which
		// login there happens to be: somebody holding both would otherwise send the donator password to
		// an internal folder and be turned away by it. None for any other host, which has no business
		// being sent one.
		static Credentials loginForUrl(const std::string& _url, const Credentials& _internal, const Credentials& _donators);
		static bool fetchChangelog(const ChangelogRequest& _request, changelogLib::Changelog& _changelog, std::string& _error);

		// Version first, which settles it without a request; then, for a channel that reuses the
		// number, the dates and the one question to the server that confirms them.
		std::optional<Asset> evaluateSelfUpdate(const Catalog& _catalog) const;

		// The logins a refresh may use, which is not quite the logins this machine has: with the team
		// login turned off, the one it keeps is not sent. Taken out of a copy, so turning the setting
		// off is not a logout - turning it back on must not ask for the password again.
		CredentialStore credentialsForFetch() const;
		static bool verifyLogin(Channel _channel, const std::string& _slug, const Credentials& _credentials,
		                        std::string& _error);

		Catalog m_catalog;
		CredentialStore m_credentials;
		InstallPaths m_installPaths;
		std::optional<Asset> m_selfUpdate;
		InstalledPlugins m_installedPlugins;

		// Bumped by refreshAsync on the message thread and only read there, so no worker sees it.
		uint32_t m_refreshGeneration = 0;

		// Cleared by the destructor, so an answer that arrives after this object is gone is dropped
		// rather than writing into it.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
