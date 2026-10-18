#include "credentials.h"

#include "catalogSource.h"
#include "loginFile.h"
#include "platformUtils.h"

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		juce::File settingsFile()
		{
			return juce::File(PlatformUtils::getSettingsPath("credentials.xml"));
		}
	}

	CredentialStore::CredentialStore(const bool _load)
	{
		if (_load)
			load();
	}

	CredentialStore::~CredentialStore() = default;

	std::string CredentialStore::keyFor(const Channel _channel, const std::string& _slug)
	{
		// One login covers every internal build, so the slug must not be part of the key - keying
		// it per product would ask the team for the same password once per product.
		if (_channel == Channel::Internal)
			return "internal";

		// Under the folder name, however it was typed: the catalog asks with the name, and a login
		// stored under the link it was pasted as would never be found again.
		if (_channel == Channel::Donators)
			return "donators:" + CatalogSource::folderName(_slug);

		return {};
	}

	Credentials CredentialStore::get(const Channel _channel, const std::string& _slug) const
	{
		const auto key = keyFor(_channel, _slug);

		if (key.empty())
			return {};

		const auto it = m_credentials.find(key);

		return it == m_credentials.end() ? Credentials{} : it->second;
	}

	std::vector<std::string> CredentialStore::getSlugs(const Channel _channel) const
	{
		std::vector<std::string> slugs;

		if (_channel != Channel::Donators)
			return slugs;

		const auto prefix = keyFor(_channel, {});

		for (const auto& [key, credentials] : m_credentials)
		{
			if (key.size() > prefix.size() && key.compare(0, prefix.size(), prefix) == 0)
				slugs.push_back(key.substr(prefix.size()));
		}

		return slugs;
	}

	void CredentialStore::set(const Channel _channel, const std::string& _slug, const Credentials& _credentials,
	                          const bool _save)
	{
		const auto key = keyFor(_channel, _slug);

		if (key.empty())
			return;

		m_credentials[key] = _credentials;

		if (_save)
			save();
	}

	void CredentialStore::clear(const Channel _channel, const std::string& _slug, const bool _save)
	{
		const auto key = keyFor(_channel, _slug);

		if (key.empty())
			return;

		m_credentials.erase(key);

		if (_save)
			save();
	}

	void CredentialStore::load()
	{
		m_credentials.clear();

		const auto xml = juce::XmlDocument::parse(settingsFile());

		if (!xml)
			return;

		for (auto* e : xml->getChildWithTagNameIterator("login"))
		{
			const auto key = e->getStringAttribute("key").toStdString();

			if (key.empty())
				continue;

			Credentials c;
			c.user = e->getStringAttribute("user").toStdString();

			// Written by a version that kept the password as it was typed. Read, and written back
			// scrambled by the next save, so nobody has to log in again to be rid of the plain copy.
			if (e->hasAttribute("password"))
			{
				c.password = e->getStringAttribute("password").toStdString();
			}
			else
			{
				c.password = LoginFile::reveal(e->getStringAttribute("pw").toStdString(), key);
			}

			if (c.valid())
				m_credentials[key] = c;
		}
	}

	void CredentialStore::save() const
	{
		juce::XmlElement root("credentials");

		for (const auto& [key, c] : m_credentials)
		{
			auto* e = root.createNewChildElement("login");
			e->setAttribute("key", key);
			e->setAttribute("user", c.user);

			// Scrambled with the entry's own key, the same way a shared login file carries one, so a
			// blob cannot be lifted from one entry into another and still decode.
			e->setAttribute("pw", LoginFile::hide(c.password, key));
		}

		const auto file = settingsFile();
		file.getParentDirectory().createDirectory();
		root.writeTo(file);

		// Every time, not only when the file is made: it may have been written by a version that
		// left it readable by everybody, and writing to it again does not change the mode it has.
		PlatformUtils::restrictToOwner(file.getFullPathName().toStdString());
	}
}
