#pragma once

#include <map>
#include <string>
#include <vector>

#include "asset.h"

namespace pluginManager
{
	struct Credentials
	{
		std::string user;
		std::string password;

		bool valid() const { return !user.empty() && !password.empty(); }
	};

	// Remembers the logins for the channels that are not public.
	//
	// Internal builds share one login across every product, donator builds have one per product,
	// which is why the lookup takes a slug that is ignored for the internal channel.
	//
	// Kept in the app's settings file, scrambled rather than encrypted and readable by nobody but
	// the user the file belongs to.
	//
	// Obfuscation is all it can be: this program has to read the password back without asking
	// anybody for anything, so the key is in the program and whoever has the program can undo it.
	// What it stops is the password being read at a glance - over a shoulder, in a screenshot, in
	// a backup, or out of a file somebody pastes into a forum - which is the leak that happens.
	// The file mode is the half that actually keeps another account out: see
	// PlatformUtils::restrictToOwner, since a settings file written with the usual umask is
	// world-readable on macOS and Linux (BUG-10460).
	//
	// A file written by an older version holds the password as it was typed; it is read as it is
	// and scrambled by the next save, so nobody loses a login over this.
	class CredentialStore
	{
	public:
		// _load is off for a test, which must neither pick up nor overwrite the logins of the machine
		// it runs on.
		explicit CredentialStore(bool _load = true);
		~CredentialStore();

		Credentials get(Channel _channel, const std::string& _slug) const;

		// _save is what tells a login that is meant to be remembered from one that was handed to this
		// run on a command line, which has no business being written into the machine's settings.
		void set(Channel _channel, const std::string& _slug, const Credentials& _credentials, bool _save = true);

		// _save as in set(): off takes the login out of this copy only, for a caller that wants to
		// go without one for a while rather than to log out.
		void clear(Channel _channel, const std::string& _slug, bool _save = true);

		bool has(Channel _channel, const std::string& _slug) const { return get(_channel, _slug).valid(); }

		// The folders a login is stored for. Only donator logins have one each; the internal login is
		// one for everything, so it has no slug to list.
		std::vector<std::string> getSlugs(Channel _channel) const;

		void load();
		void save() const;

	private:
		static std::string keyFor(Channel _channel, const std::string& _slug);

		std::map<std::string, Credentials> m_credentials;
	};
}
