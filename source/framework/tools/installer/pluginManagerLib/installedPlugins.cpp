#include "installedPlugins.h"

#include <algorithm>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		juce::File manifestFile()
		{
			return juce::File(PlatformUtils::getSettingsPath("installed.json"));
		}
	}

	InstalledPlugins::InstalledPlugins(const bool _load)
	{
		if (_load)
			load();
	}

	void InstalledPlugins::set(const Asset& _asset, const std::string& _dir, std::vector<std::string> _files,
	                           const int64_t _modified)
	{
		const auto format = PlatformUtils::formatFromString(_asset.format);
		const auto path = PlatformUtils::installedFilePath(_asset.product, format, _dir);

		if (path.empty())
			return;

		const auto now = juce::Time::getCurrentTime().toISO8601(true).toStdString();

		for (auto& entry : m_entries)
		{
			if (entry.product != _asset.product || entry.format != _asset.format)
				continue;

			entry.version = _asset.version;
			entry.path = path;
			entry.time = now;
			entry.listed = _asset.listed;
			entry.modified = _modified;
			entry.files = std::move(_files);
			return;
		}

		m_entries.push_back({_asset.product, _asset.format, _asset.version, path, now,
		                     _asset.listed, _modified, std::move(_files)});
	}

	const InstalledPlugins::Entry* InstalledPlugins::findEntry(const std::string& _product,
	                                                          const PluginFormat _format,
	                                                          const std::string& _dir) const
	{
		const auto format = PlatformUtils::formatToString(_format);
		const auto dir = juce::File(juce::String(_dir));

		for (const auto& entry : m_entries)
		{
			if (entry.product != _product || entry.format != format)
				continue;

			if (juce::File(juce::String(entry.path)).getParentDirectory() == dir)
				return &entry;
		}

		return nullptr;
	}

	std::string InstalledPlugins::installedFile(const std::string& _product, const PluginFormat _format,
	                                            const std::string& _dir) const
	{
		if (const auto* entry = findEntry(_product, _format, _dir))
		{
			// Any of them: the one that is the program is not marked out in there, and a folder this
			// install still has files in is a folder it is still installed in.
			for (const auto& name : entry->files)
			{
				const auto file = juce::File(juce::String(_dir)).getChildFile(juce::String(name));

				if (file.existsAsFile() || file.isDirectory())
					return file.getFullPathName().toStdString();
			}
		}

		// Nothing written down, or written down before the file names were: the name the product
		// would have is all there is to go on.
		const auto path = PlatformUtils::installedFilePath(_product, _format, _dir);

		if (path.empty() || !juce::File(juce::String(path)).exists())
			return {};

		return path;
	}

	std::string InstalledPlugins::getVersion(const std::string& _product, const PluginFormat _format,
	                                         const std::string& _dir) const
	{
		// A record whose files are all gone is not an install, so there is no version to report.
		if (installedFile(_product, _format, _dir).empty())
			return {};

		if (const auto* entry = findEntry(_product, _format, _dir))
			return entry->version;

		// Not ours, or installed before this program existed. The file may still say.
		return PlatformUtils::getInstalledVersion(_product, _format, _dir);
	}

	std::vector<std::string> InstalledPlugins::filesToRemove(const std::string& _product,
	                                                        const PluginFormat _format,
	                                                        const std::string& _dir,
	                                                        const std::vector<std::pair<std::string, PluginFormat>>& _alsoGoing) const
	{
		const auto format = PlatformUtils::formatToString(_format);

		const Entry* mine = nullptr;

		for (const auto& entry : m_entries)
		{
			if (entry.product == _product && entry.format == format)
				mine = &entry;
		}

		if (!mine || mine->files.empty())
			return {};

		const auto dir = juce::File(juce::String(mine->path)).getParentDirectory();

		// A record of an install in another folder says nothing about this one.
		if (dir != juce::File(juce::String(_dir)))
			return {};

		// Anything another install put in the same folder under the same name stays: it is as much
		// theirs as ours, and they are not being uninstalled.
		const auto claimedByAnother = [&](const std::string& _name)
		{
			for (const auto& entry : m_entries)
			{
				if (&entry == mine || entry.files.empty())
					continue;

				const auto going = std::find_if(_alsoGoing.begin(), _alsoGoing.end(), [&](const auto& _other)
				{
					return _other.first == entry.product && PlatformUtils::formatToString(_other.second) == entry.format;
				});

				if (going != _alsoGoing.end())
					continue;

				if (juce::File(juce::String(entry.path)).getParentDirectory() != dir)
					continue;

				if (std::find(entry.files.begin(), entry.files.end(), _name) != entry.files.end())
					return true;
			}

			return false;
		};

		std::vector<std::string> paths;

		for (const auto& name : mine->files)
		{
			if (claimedByAnother(name))
				continue;

			paths.push_back(dir.getChildFile(juce::String(name)).getFullPathName().toStdString());
		}

		return paths;
	}

	void InstalledPlugins::remove(const std::string& _product, const PluginFormat _format)
	{
		const auto format = PlatformUtils::formatToString(_format);

		for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
		{
			if (it->product != _product || it->format != format)
				continue;

			m_entries.erase(it);
			return;
		}
	}

	void InstalledPlugins::load()
	{
		m_entries.clear();

		const auto parsed = juce::JSON::parse(manifestFile());

		if (!parsed.isArray())
			return;

		for (const auto& item : *parsed.getArray())
		{
			const auto* obj = item.getDynamicObject();

			if (!obj)
				continue;

			Entry entry;
			entry.product = obj->getProperty("product").toString().toStdString();
			entry.format = obj->getProperty("format").toString().toStdString();
			entry.version = obj->getProperty("version").toString().toStdString();
			entry.path = obj->getProperty("path").toString().toStdString();
			entry.time = obj->getProperty("time").toString().toStdString();
			entry.listed = obj->getProperty("listed").toString().toStdString();
			// Through juce::int64 rather than straight to int64_t: the two are the same width but not
			// the same type where long is 64 bit, and var converts to one of them and not the other.
			entry.modified = static_cast<juce::int64>(obj->getProperty("modified"));

			// Absent in a manifest written before installs said what they wrote.
			const auto files = obj->getProperty("files");

			if (files.isArray())
			{
				for (const auto& name : *files.getArray())
				{
					const auto file = name.toString().toStdString();

					if (!file.empty())
						entry.files.push_back(file);
				}
			}

			if (!entry.product.empty() && !entry.version.empty() && !entry.path.empty())
				m_entries.push_back(std::move(entry));
		}
	}

	void InstalledPlugins::save() const
	{
		auto array = juce::Array<juce::var>();

		for (const auto& entry : m_entries)
		{
			// An entry with nothing of it left on disk is dropped rather than written out again: it was
			// uninstalled or moved by other means, and this is the only chance to notice. Asked of what
			// the install wrote, not of the name the product would have - a program keeps the name it
			// was built under, so that name is never there and every record of one was dropped here as
			// soon as it was made.
			const auto dir = juce::File(juce::String(entry.path)).getParentDirectory()
				.getFullPathName().toStdString();

			if (installedFile(entry.product, PlatformUtils::formatFromString(entry.format), dir).empty())
				continue;

			auto* obj = new juce::DynamicObject();

			obj->setProperty("product", juce::String(entry.product));
			obj->setProperty("format", juce::String(entry.format));
			obj->setProperty("version", juce::String(entry.version));
			obj->setProperty("path", juce::String(entry.path));
			obj->setProperty("time", juce::String(entry.time));

			// Only when there is one, so the file of somebody who installs nothing but public builds
			// does not fill up with empty fields.
			if (!entry.listed.empty())
				obj->setProperty("listed", juce::String(entry.listed));

			if (entry.modified)
				obj->setProperty("modified", juce::var(static_cast<juce::int64>(entry.modified)));

			if (!entry.files.empty())
			{
				juce::Array<juce::var> files;

				for (const auto& file : entry.files)
					files.add(juce::var(juce::String(file)));

				obj->setProperty("files", juce::var(files));
			}

			array.add(juce::var(obj));
		}

		const auto file = manifestFile();
		file.getParentDirectory().createDirectory();
		file.replaceWithText(juce::JSON::toString(juce::var(array), false));
	}
}
