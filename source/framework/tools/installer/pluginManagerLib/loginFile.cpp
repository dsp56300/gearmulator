#include "loginFile.h"

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		// Says a file or a pasted line is one of ours before anything in it is believed. Without it
		// any JSON file dropped on the window would be read as a login that happened to carry none.
		constexpr const char* g_marker = "tusLogin";
		constexpr int g_version = 1;

		// Not a secret. See the header: this is what makes the password unreadable at a glance, and
		// nothing more.
		constexpr const char* g_salt = "TheUsualSuspects/pluginManager/login";

		// Both run through every byte rather than one after the other. Joined end to end, a password
		// shorter than the salt never reaches the slug at all, and then the folder it belongs to
		// makes no difference to the result - which is the one thing this is supposed to guarantee.
		std::string mask(const std::string& _text, const std::string& _slug)
		{
			const std::string salt = g_salt;

			std::string out = _text;

			for (size_t i = 0; i < out.size(); ++i)
			{
				auto c = static_cast<char>(out[i] ^ salt[i % salt.size()]);

				if (!_slug.empty())
					c = static_cast<char>(c ^ _slug[i % _slug.size()]);

				out[i] = c;
			}

			return out;
		}
	}

	std::string LoginFile::hide(const std::string& _password, const std::string& _slug)
	{
		const auto masked = mask(_password, _slug);

		return juce::Base64::toBase64(masked.data(), masked.size()).toStdString();
	}

	std::string LoginFile::reveal(const std::string& _hidden, const std::string& _slug)
	{
		juce::MemoryOutputStream decoded;

		if (!juce::Base64::convertFromBase64(decoded, juce::String(_hidden)))
			return {};

		return mask(std::string(static_cast<const char*>(decoded.getData()), decoded.getDataSize()), _slug);
	}

	bool LoginFile::parse(const std::string& _text, std::vector<SharedLogin>& _logins)
	{
		_logins.clear();

		// A file of any size can be dropped on the window, and most of them are not this.
		if (_text.empty() || _text.size() > 64 * 1024)
			return false;

		juce::var json;

		if (juce::JSON::parse(juce::String(_text), json).failed() || !json.isObject())
			return false;

		if (static_cast<int>(json[g_marker]) != g_version)
			return false;

		const auto* logins = json["logins"].getArray();

		if (!logins)
			return false;

		for (const auto& entry : *logins)
		{
			SharedLogin login;

			login.slug = entry["slug"].toString().toStdString();
			login.credentials.user = entry["user"].toString().toStdString();
			login.credentials.password = reveal(entry["pw"].toString().toStdString(), login.slug);

			// All three or none of it. Half a login cannot be offered to anybody: it would be a
			// confirmation box asking about a folder nothing can be fetched from.
			if (login.slug.empty() || !login.credentials.valid())
			{
				_logins.clear();
				return false;
			}

			_logins.push_back(std::move(login));
		}

		return !_logins.empty();
	}

	std::string LoginFile::write(const std::vector<SharedLogin>& _logins)
	{
		if (_logins.empty())
			return {};

		juce::Array<juce::var> logins;

		for (const auto& login : _logins)
		{
			auto* entry = new juce::DynamicObject();

			entry->setProperty("slug", juce::String(login.slug));
			entry->setProperty("user", juce::String(login.credentials.user));
			entry->setProperty("pw", juce::String(hide(login.credentials.password, login.slug)));

			logins.add(juce::var(entry));
		}

		auto* root = new juce::DynamicObject();

		root->setProperty(g_marker, g_version);
		root->setProperty("logins", juce::var(logins));

		// On one line, so the same text works as a file to send and as something to paste.
		return juce::JSON::toString(juce::var(root), true).toStdString();
	}
}
