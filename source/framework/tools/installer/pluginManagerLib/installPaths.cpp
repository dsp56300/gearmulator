#include "installPaths.h"

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		juce::File settingsFile()
		{
			return juce::File(PlatformUtils::getSettingsPath("settings.xml"));
		}
	}

	InstallPaths::InstallPaths(const bool _load)
	{
		if (_load)
			load();
	}

	std::string InstallPaths::keyFor(const std::string& _product, const PluginFormat _format)
	{
		return _product + '/' + PlatformUtils::formatToString(_format);
	}

	std::string InstallPaths::get(const PluginFormat _format) const
	{
		const auto it = m_paths.find(_format);

		if (it != m_paths.end())
			return it->second;

		return getDefault(_format);
	}

	std::string InstallPaths::get(const std::string& _product, const PluginFormat _format) const
	{
		const auto it = m_productPaths.find(keyFor(_product, _format));

		if (it != m_productPaths.end())
			return it->second;

		return get(_format);
	}

	bool InstallPaths::isCustom(const PluginFormat _format) const
	{
		return m_paths.find(_format) != m_paths.end();
	}

	bool InstallPaths::isCustom(const std::string& _product, const PluginFormat _format) const
	{
		return m_productPaths.find(keyFor(_product, _format)) != m_productPaths.end();
	}

	void InstallPaths::set(const PluginFormat _format, const std::string& _path)
	{
		if (_path.empty() || _path == getDefault(_format))
			m_paths.erase(_format);
		else
			m_paths[_format] = _path;
	}

	void InstallPaths::set(const std::string& _product, const PluginFormat _format, const std::string& _path)
	{
		const auto key = keyFor(_product, _format);

		// Where the format already goes is not worth remembering per product - and stops the entry
		// pinning this product if the format is moved later.
		if (_path.empty() || _path == get(_format))
			m_productPaths.erase(key);
		else
			m_productPaths[key] = _path;
	}

	void InstallPaths::setSelfBuild(std::string _listed, const int64_t _modified)
	{
		m_selfListed = std::move(_listed);
		m_selfModified = _modified;
	}

	void InstallPaths::setHidden(const std::string& _product, const bool _hidden)
	{
		if (_hidden)
			m_hidden.insert(_product);
		else
			m_hidden.erase(_product);
	}

	void InstallPaths::setCollapsed(const std::string& _category, const bool _collapsed)
	{
		if (_collapsed)
			m_collapsed.insert(_category);
		else
			m_collapsed.erase(_category);
	}

	std::vector<PluginFormat> InstallPaths::getFormats()
	{
		std::vector<PluginFormat> formats{PluginFormat::VST2, PluginFormat::VST3, PluginFormat::CLAP,
		                                  PluginFormat::LV2};

		// There is no AU anywhere else, and no AU build in the catalog for anywhere else either. A
		// location for one would be a setting nobody here can use.
#if JUCE_MAC
		formats.push_back(PluginFormat::AU);
#endif

		formats.push_back(PluginFormat::Standalone);

		return formats;
	}

	void InstallPaths::load()
	{
		m_paths.clear();
		m_productPaths.clear();
		m_otherSettings.clear();
		m_teamLogin = false;
		m_hidden.clear();
		m_collapsed.clear();
		m_selfListed.clear();
		m_selfModified = 0;

		const auto xml = juce::XmlDocument::parse(settingsFile());

		if (!xml)
			return;

		for (auto* e : xml->getChildIterator())
		{
			if (e->hasTagName("installPath"))
			{
				const auto format = PlatformUtils::formatFromString(e->getStringAttribute("format").toStdString());
				const auto path = e->getStringAttribute("path").toStdString();
				const auto product = e->getStringAttribute("product").toStdString();

				if (format == PluginFormat::Count || path.empty())
					continue;

				if (product.empty())
					m_paths[format] = path;
				else
					m_productPaths[keyFor(product, format)] = path;

				continue;
			}

			// Taken out of the passthrough below rather than carried through it, because save() writes
			// this one itself now. Left in both it would be written twice.
			if (e->hasTagName("teamLogin"))
			{
				m_teamLogin = e->getIntAttribute("enabled", 1) != 0;
				continue;
			}

			if (e->hasTagName("selfBuild"))
			{
				m_selfListed = e->getStringAttribute("listed").toStdString();
				m_selfModified = static_cast<int64_t>(e->getDoubleAttribute("modified"));
				continue;
			}

			if (e->hasTagName("hidden"))
			{
				const auto product = e->getStringAttribute("product").toStdString();

				if (!product.empty())
					m_hidden.insert(product);

				continue;
			}

			// Without a category is a group too, so an empty one is kept.
			if (e->hasTagName("collapsed"))
			{
				m_collapsed.insert(e->getStringAttribute("category").toStdString());
				continue;
			}

			// Was which of the two lists to show, and there is one list now. Read and dropped rather
			// than kept with the rest of what this loop does not recognise, so the line goes out of
			// the file the next time it is written instead of being carried for ever.
			if (e->hasTagName("view"))
				continue;

			// Kept whether it is understood or not, so that writing the file back does not throw
			// away what somebody put there.
			m_otherSettings.push_back(e->toString(juce::XmlElement::TextFormat().singleLine()).toStdString());
		}
	}

	void InstallPaths::save() const
	{
		juce::XmlElement xml("pluginManager");

		for (const auto& [format, path] : m_paths)
		{
			auto* e = xml.createNewChildElement("installPath");
			e->setAttribute("format", juce::String(PlatformUtils::formatToString(format)));
			e->setAttribute("path", juce::String(path));
		}

		for (const auto& [key, path] : m_productPaths)
		{
			const auto slash = key.rfind('/');

			auto* e = xml.createNewChildElement("installPath");
			e->setAttribute("product", juce::String(key.substr(0, slash)));
			e->setAttribute("format", juce::String(key.substr(slash + 1)));
			e->setAttribute("path", juce::String(path));
		}

		// Only when it is on. Writing enabled="0" into everybody's file would announce a feature
		// most people have no use for, and off is what its absence already means.
		if (m_teamLogin)
		{
			auto* e = xml.createNewChildElement("teamLogin");
			e->setAttribute("enabled", 1);
		}

		if (!m_selfListed.empty() || m_selfModified)
		{
			auto* e = xml.createNewChildElement("selfBuild");
			e->setAttribute("listed", juce::String(m_selfListed));
			e->setAttribute("modified", static_cast<double>(m_selfModified));
		}

		for (const auto& product : m_hidden)
		{
			auto* e = xml.createNewChildElement("hidden");
			e->setAttribute("product", juce::String(product));
		}

		for (const auto& category : m_collapsed)
		{
			auto* e = xml.createNewChildElement("collapsed");
			e->setAttribute("category", juce::String(category));
		}

		for (const auto& other : m_otherSettings)
		{
			if (auto element = juce::parseXML(juce::String(other)))
				xml.addChildElement(element.release());
		}

		const auto file = settingsFile();
		file.getParentDirectory().createDirectory();
		xml.writeTo(file);
	}
}
