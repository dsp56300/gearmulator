#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "platformUtils.h"

namespace pluginManager
{
	// Where each format is installed.
	//
	// Three answers, most specific first: what this product was pointed at, what this format was
	// pointed at, and what the platform expects. Every format has a default except Standalone, which
	// has no convention to follow and is only installable once a location is set.
	//
	// Changes are kept until save() is called, so a test can move them around without writing into
	// the settings of the machine it runs on.
	class InstallPaths
	{
	public:
		explicit InstallPaths(bool _load = true);

		// The location in use for a format everywhere: what was set for it, or the platform's default.
		std::string get(PluginFormat _format) const;

		// The location in use for one product, which may have been pointed somewhere of its own.
		std::string get(const std::string& _product, PluginFormat _format) const;

		// The default, whether or not something else is set.
		static std::string getDefault(PluginFormat _format) { return PlatformUtils::getDefaultPluginPath(_format); }

		bool isCustom(PluginFormat _format) const;
		bool isCustom(const std::string& _product, PluginFormat _format) const;

		// An empty path resets the format to its default.
		void set(PluginFormat _format, const std::string& _path);

		// An empty path puts this product back to wherever the format goes.
		void set(const std::string& _product, PluginFormat _format, const std::string& _path);

		// The formats worth showing a location for: everything a build can exist in on THIS system,
		// which is not the same as everything this program knows about. Not only the ones with a
		// default here - Standalone has none anywhere - but not AU off macOS either, where there is
		// no such plugin and no build of one.
		static std::vector<PluginFormat> getFormats();

		// Whether the team login may be offered at all. Deliberately not in the window: somebody
		// who has no reason to know the login exists never sees it. `tuspm teamlogin on` is how it
		// is turned on, rather than by hand - see the comment on save() for why editing the file
		// was a trap.
		bool hasTeamLogin() const { return m_teamLogin; }
		void setTeamLogin(const bool _enabled) { m_teamLogin = _enabled; }

		// Rows the user has taken out of the list. By product id rather than by name, because the
		// name is what a product gets renamed to and the id is what everything else here uses.
		// Nothing is uninstalled by hiding it: a hidden product that is installed stays installed,
		// and showing it again brings the row back as it was.
		// The age of the build this program is running, as the server stated it when it was fetched.
		// Not in installed.json with the plugins: that is keyed by the folder a format installs to,
		// and this program lives wherever somebody put it. Empty until it has updated itself once,
		// which is what makes a hand-unpacked copy fall back to comparing version numbers.
		const std::string& getSelfListed() const { return m_selfListed; }
		int64_t getSelfModified() const { return m_selfModified; }
		void setSelfBuild(std::string _listed, int64_t _modified);

		bool isHidden(const std::string& _product) const { return m_hidden.find(_product) != m_hidden.end(); }
		void setHidden(const std::string& _product, bool _hidden);
		void showAll() { m_hidden.clear(); }
		size_t getHiddenCount() const { return m_hidden.size(); }

		// Category groups the user has folded up in the list, by the category as the product lists
		// spell it. Empty is the group of products without one.
		bool isCollapsed(const std::string& _category) const { return m_collapsed.find(_category) != m_collapsed.end(); }
		void setCollapsed(const std::string& _category, bool _collapsed);

		void load();
		void save() const;

	private:
		static std::string keyFor(const std::string& _product, PluginFormat _format);

		std::map<PluginFormat, std::string> m_paths;		// the formats that differ from the default
		std::map<std::string, std::string> m_productPaths;	// the products that differ from their format

		bool m_teamLogin = false;

		std::string m_selfListed;							// the listing date of the build this program is
		int64_t m_selfModified = 0;							// and its Last-Modified, in UTC seconds

		std::set<std::string> m_hidden;						// the products that are not shown in the list
		std::set<std::string> m_collapsed;					// the category groups shown folded up

		// Whatever else is in the file, kept as it was read. A line someone added by hand - the team
		// login is one - must still be there after this program writes the file again.
		std::vector<std::string> m_otherSettings;
	};
}
