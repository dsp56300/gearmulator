#include "offlineFolder.h"

#include "catalogSource.h"
#include "httpClient.h"
#include "platformUtils.h"

#include <algorithm>
#include <set>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		const char* channelName(const Channel _channel)
		{
			switch (_channel)
			{
			case Channel::Internal: return "internal";
			case Channel::Donators: return "donators";
			default:                return "public";
			}
		}

		Channel channelFrom(const juce::String& _name)
		{
			if (_name == "internal")
				return Channel::Internal;

			if (_name == "donators")
				return Channel::Donators;

			return Channel::Public;
		}

		// "2026-10-05 18:37" in UTC, the shape a listing prints, for a build that only its file can date.
		std::string stampOf(const juce::Time& _time)
		{
			const auto utc = _time - juce::RelativeTime::seconds(_time.getUTCOffsetSeconds());
			return utc.formatted("%Y-%m-%d %H:%M").toStdString();
		}

		// The other way round, which gives a kept archive the date it was listed with.
		bool timeOf(const std::string& _listed, juce::Time& _time)
		{
			if (_listed.size() < 16)
				return false;

			const auto number = [&_listed](const size_t _at, const size_t _length)
			{
				return juce::String(_listed.substr(_at, _length)).getIntValue();
			};

			_time = juce::Time(number(0, 4), number(5, 2) - 1, number(8, 2), number(11, 2), number(14, 2), 0, 0, false);
			return true;
		}

		// A path in offline.json, relative to the folder, as the URL the library fetches it with. Anything
		// else - a URL somebody wrote in by hand - is taken as it is.
		std::string resolve(const juce::File& _folder, const std::string& _path)
		{
			if (_path.empty() || _path.find("://") != std::string::npos)
				return _path;

			const auto path = PlatformUtils::inside(_folder.getFullPathName().toStdString(), _path);

			return path.empty() ? std::string() : HttpClient::fileUrl(path);
		}

		// Where a file a product brings is kept, relative to the folder: beneath the product and the
		// folder of its data folder it is for, under the name it is installed with.
		std::string relativePathOf(const std::string& _product, const FileSection& _section,
		                           const FileSection::Download& _download)
		{
			auto path = _download.path;

			if (path.empty() || path.back() == '/' || path.back() == '\\')
				path += HttpClient::fileNameOf(_download.url);

			return "files/" + _product + "/" + _section.folder + "/" + path;
		}

		// Beside the target first, so that a download that breaks off does not leave half a file in place
		// of a whole one, and then dated as the server dates it.
		bool fetch(const std::string& _url, const juce::File& _target, const Credentials& _login, std::string& _error)
		{
			if (!_target.getParentDirectory().createDirectory())
			{
				_error = "Could not create " + _target.getParentDirectory().getFullPathName().toStdString();
				return false;
			}

			const auto partial = _target.getSiblingFile(_target.getFileName() + ".tus-download");

			if (!HttpClient::downloadFile(_url, partial.getFullPathName().toStdString(), _error, {}, {}, _login))
			{
				partial.deleteFile();
				return false;
			}

			if (!partial.moveFileTo(_target))
			{
				partial.deleteFile();
				_error = "Could not write " + _target.getFullPathName().toStdString();
				return false;
			}

			int64_t modified = 0;
			std::string ignored;

			if (HttpClient::fetchLastModified(_url, modified, ignored, _login))
				_target.setLastModificationTime(juce::Time(modified * 1000));

			return true;
		}

		juce::var toVar(const Product& _product)
		{
			auto* obj = new juce::DynamicObject();

			const auto set = [obj](const char* _name, const std::string& _value)
			{
				if (!_value.empty())
					obj->setProperty(_name, juce::String(_value));
			};

			set("product", _product.product);

			// Always, empty or not: left out, it would be read as the lowercase product name.
			obj->setProperty("slug", juce::String(_product.slug));

			set("name", _product.name);
			set("description", _product.description);
			set("thumbnail", _product.thumbnail);
			set("category", _product.category);
			set("manufacturer", _product.manufacturer);
			set("originalName", _product.originalName);

			if (_product.year > 0)
				obj->setProperty("year", _product.year);

			juce::Array<juce::var> sections;

			for (const auto& section : _product.files)
			{
				auto* s = new juce::DynamicObject();
				s->setProperty("folder", juce::String(section.folder));

				juce::Array<juce::var> urls;

				for (const auto& file : section.files)
				{
					auto* f = new juce::DynamicObject();
					f->setProperty("url", juce::String(file.url));

					if (!file.path.empty())
						f->setProperty("path", juce::String(file.path));

					urls.add(juce::var(f));
				}

				s->setProperty("urls", urls);
				sections.add(juce::var(s));
			}

			if (!sections.isEmpty())
				obj->setProperty("files", sections);

			return juce::var(obj);
		}
	}

	OfflineFolder::OfflineFolder(std::string _dir) : m_dir(std::move(_dir))
	{
		const auto file = juce::File(juce::String(m_dir)).getChildFile(g_manifest);

		if (!file.existsAsFile())
			return;

		const auto text = file.loadFileAsString();

		// The products in the same shape as every product list, so the one parser reads them.
		for (auto& product : CatalogSource::parseProducts(text.toStdString()))
		{
			const auto id = product.product;
			m_products[id] = std::move(product);
		}

		juce::var parsed;

		if (juce::JSON::parse(text, parsed).failed())
			return;

		const auto builds = parsed.getProperty("builds", {});

		if (!builds.isArray())
			return;

		for (const auto& entry : *builds.getArray())
		{
			const auto name = entry.getProperty("file", {}).toString().toStdString();

			if (name.empty())
				continue;

			Build build;
			build.channel = channelFrom(entry.getProperty("channel", {}).toString());
			build.listed = entry.getProperty("listed", {}).toString().toStdString();

			m_builds[name] = build;
		}
	}

	std::mutex& OfflineFolder::writeLock()
	{
		static std::mutex mutex;
		return mutex;
	}

	bool OfflineFolder::hasManifest(const std::string& _dir)
	{
		return !_dir.empty() && juce::File(juce::String(_dir)).getChildFile(g_manifest).existsAsFile();
	}

	std::string OfflineFolder::archiveName(const Asset& _asset)
	{
		return HttpClient::fileNameOf(_asset.url);
	}

	std::string OfflineFolder::archivePath(const Asset& _asset) const
	{
		const auto name = archiveName(_asset);

		return name.empty() ? std::string()
			: juce::File(juce::String(m_dir)).getChildFile(juce::String(name)).getFullPathName().toStdString();
	}

	bool OfflineFolder::read(const std::string& _dir, std::vector<Asset>& _assets, std::vector<Product>& _products,
	                         std::string& _error)
	{
		_assets.clear();
		_products.clear();

		const auto folder = juce::File(juce::String(_dir));

		if (!folder.isDirectory())
		{
			_error = _dir + " is not a folder.";
			return false;
		}

		const OfflineFolder manifest(_dir);

		// What the name says is all an archive needs; what offline.json adds is where it came from.
		for (const auto& file : folder.findChildFiles(juce::File::findFiles, false))
		{
			const auto name = file.getFileName().toStdString();

			Asset asset;

			if (!parseAssetName(name, asset))
				continue;

			asset.url = HttpClient::fileUrl(file.getFullPathName().toStdString());

			const auto it = manifest.m_builds.find(name);

			if (it != manifest.m_builds.end())
			{
				asset.channel = it->second.channel;
				asset.listed = it->second.listed;
			}
			else
			{
				asset.channel = Channel::Public;
				asset.listed = stampOf(file.getLastModificationTime());
			}

			_assets.push_back(std::move(asset));
		}

		if (_assets.empty())
		{
			_error = "There is no build in " + _dir + ".";
			return false;
		}

		for (const auto& [id, stored] : manifest.m_products)
		{
			auto product = stored;

			product.thumbnail = resolve(folder, product.thumbnail);

			for (auto& section : product.files)
			{
				for (auto& file : section.files)
					file.url = resolve(folder, file.url);
			}

			_products.push_back(std::move(product));
		}

		// The builds offline.json says nothing about get a product named after them, as the builds behind
		// a donator login do. A variant of a product that is in here - the effect build of a plugin, its
		// test console - is shown with that product rather than on a row of its own.
		std::vector<std::string> named;

		for (const auto& asset : _assets)
		{
			if (std::find(named.begin(), named.end(), asset.product) == named.end())
				named.push_back(asset.product);
		}

		const auto isVariantOf = [](const std::string& _product, const std::string& _base)
		{
			return _product.size() > _base.size() && _product.compare(0, _base.size(), _base) == 0;
		};

		for (const auto& id : named)
		{
			const auto known = std::any_of(_products.begin(), _products.end(), [&](const Product& _p)
			{
				return _p.product == id || isVariantOf(id, _p.product);
			});

			const auto variant = std::any_of(named.begin(), named.end(), [&](const std::string& _other)
			{
				return isVariantOf(id, _other);
			});

			if (known || variant)
				continue;

			Product product;
			product.product = id;
			product.name = id;

			_products.push_back(std::move(product));
		}

		return true;
	}

	bool OfflineFolder::hasBuild(const Asset& _asset) const
	{
		const auto name = archiveName(_asset);
		const auto it = m_builds.find(name);

		return it != m_builds.end() && it->second.listed == _asset.listed
			&& juce::File(juce::String(archivePath(_asset))).getSize() > 0;
	}

	void OfflineFolder::addBuild(const Asset& _asset)
	{
		const auto name = archiveName(_asset);

		if (name.empty())
			return;

		m_builds[name] = {_asset.channel, _asset.listed};

		juce::Time listed;

		if (timeOf(_asset.listed, listed))
			juce::File(juce::String(archivePath(_asset))).setLastModificationTime(listed);
	}

	bool OfflineFolder::hasProduct(const Product& _product, const std::vector<FileSection>& _files) const
	{
		const auto it = m_products.find(_product.product);

		if (it == m_products.end())
			return false;

		const auto& stored = it->second;

		if (!_product.thumbnail.empty() && !juce::File(juce::String(PlatformUtils::inside(m_dir, stored.thumbnail))).existsAsFile())
			return false;

		std::set<std::string> kept;

		for (const auto& section : stored.files)
		{
			for (const auto& file : section.files)
				kept.insert(file.url);
		}

		for (const auto& section : _files)
		{
			for (const auto& file : section.files)
			{
				const auto path = relativePathOf(_product.product, section, file);

				if (!kept.count(path) || !juce::File(juce::String(PlatformUtils::inside(m_dir, path))).existsAsFile())
					return false;
			}
		}

		return true;
	}

	bool OfflineFolder::addProduct(const Product& _product, const std::vector<FileSection>& _files,
	                               const LoginForUrl& _login, std::string& _error)
	{
		_error.clear();

		const auto fail = [&_error](const std::string& _what)
		{
			if (_error.empty())
				_error = _what;
		};

		auto stored = _product;
		stored.thumbnail.clear();
		stored.files.clear();

		const auto folder = juce::File(juce::String(m_dir));

		// The picture is worth having but not worth failing over: a row shows a placeholder without one.
		if (!_product.thumbnail.empty())
		{
			const auto name = HttpClient::fileNameOf(_product.thumbnail);
			const auto path = "thumbnails/" + name;
			std::string error;

			if (!name.empty() && fetch(_product.thumbnail, folder.getChildFile(juce::String(path)), _login(_product.thumbnail), error))
				stored.thumbnail = path;
			else
				fail(_product.name + ": the picture: " + error);
		}

		for (const auto& section : _files)
		{
			FileSection kept;
			kept.folder = section.folder;

			for (const auto& file : section.files)
			{
				const auto path = relativePathOf(_product.product, section, file);
				const auto target = PlatformUtils::inside(m_dir, path);

				if (target.empty())
				{
					fail("\"" + path + "\" is not a path inside the folder");
					continue;
				}

				std::string error;

				if (!fetch(file.url, juce::File(juce::String(target)), _login(file.url), error))
				{
					fail(file.url + ": " + error);
					continue;
				}

				// Installed with the same path as from the server, so it lands where it always did.
				kept.files.push_back({path, file.path});
			}

			if (!kept.files.empty())
				stored.files.push_back(std::move(kept));
		}

		m_products[_product.product] = std::move(stored);

		return _error.empty();
	}

	bool OfflineFolder::save(std::string& _error) const
	{
		const auto folder = juce::File(juce::String(m_dir));

		if (!folder.createDirectory())
		{
			_error = "Could not create " + m_dir;
			return false;
		}

		juce::Array<juce::var> products;

		for (const auto& [id, product] : m_products)
			products.add(toVar(product));

		juce::Array<juce::var> builds;

		for (const auto& [name, build] : m_builds)
		{
			auto* b = new juce::DynamicObject();
			b->setProperty("file", juce::String(name));
			b->setProperty("channel", channelName(build.channel));
			b->setProperty("listed", juce::String(build.listed));
			builds.add(juce::var(b));
		}

		auto* root = new juce::DynamicObject();
		root->setProperty("tusOffline", 1);
		root->setProperty("products", products);
		root->setProperty("builds", builds);

		const auto file = folder.getChildFile(g_manifest);

		if (!file.replaceWithText(juce::JSON::toString(juce::var(root))))
		{
			_error = "Could not write " + file.getFullPathName().toStdString();
			return false;
		}

		return true;
	}
}
