#include "pluginManager.h"

#include "catalogSource.h"
#include "config.h"
#include "elevatedTask.h"
#include "httpClient.h"
#include "log.h"
#include "pluginInstaller.h"
#include "selfUpdate.h"

#include <algorithm>
#include <map>
#include <mutex>

#include "juce_core/juce_core.h"
#include "juce_events/juce_events.h"

namespace pluginManager
{
	namespace
	{
		// A plugin in most formats IS a folder - Osirus.vst3 on every system, a .component, a .lv2 -
		// so walking into one means walking a whole Contents tree that cannot hold another plugin.
		// Skipping them took the Common Files VST3 folder of a machine with a lot of plugins from 179
		// folders to a handful.
		bool isBundle(const juce::File& _dir)
		{
			const auto extension = _dir.getFileExtension().toLowerCase();

			return extension == ".vst3" || extension == ".vst" || extension == ".clap"
				|| extension == ".lv2" || extension == ".component" || extension == ".app";
		}

		// Every folder under this one that could hold a plugin, itself first.
		void collectFolders(const juce::File& _dir, std::vector<std::string>& _folders, const int _depth)
		{
			_folders.push_back(_dir.getFullPathName().toStdString());

			// Nobody files plugins eight folders deep, and the two limits are what keeps a location
			// somebody pointed at a whole drive from being walked to the bottom of it.
			if (_depth >= 8 || _folders.size() >= 4096)
				return;

			for (const auto& child : _dir.findChildFiles(juce::File::findDirectories, false))
			{
				if (!isBundle(child))
					collectFolders(child, _folders, _depth + 1);
			}
		}

		// The folders under a location, kept for a moment.
		//
		// The walk costs tens of milliseconds where there are many plugins, and the question behind it
		// is asked once per product per format - hundreds of times in a row while the list is drawn.
		//
		// Only the LIST of folders is kept. Whether a plugin is in one of them is asked of the file
		// system every time, so a list that has gone stale costs a look into a folder that is no longer
		// there, or a second before one that has just appeared is noticed - never a wrong answer about
		// what is installed.
		std::vector<std::string> foldersUnder(const std::string& _dir)
		{
			static std::mutex mutex;
			static std::map<std::string, std::pair<juce::int64, std::vector<std::string>>> cache;

			constexpr juce::int64 keepFor = 1000;

			const auto now = juce::Time::getMillisecondCounterHiRes();

			std::lock_guard lock(mutex);

			auto& entry = cache[_dir];

			if (entry.second.empty() || static_cast<juce::int64>(now) - entry.first > keepFor)
			{
				entry.first = static_cast<juce::int64>(now);
				entry.second.clear();

				const auto dir = juce::File(juce::String(_dir));

				if (dir.isDirectory())
					collectFolders(dir, entry.second, 0);
				else
					entry.second.push_back(_dir);	// asked about all the same, and answered by the file check
			}

			return entry.second;
		}

		// Whether a file is clearly older than a build listed at _listed, "2026-10-13 09:37". That is read
		// as local time, with four hours to spare, because it is not quite true - the listing has the
		// server's clock, a release has UTC - and an archive carries the build's own date rather than the
		// upload's.
		bool isClearlyOlderThan(const juce::File& _file, const std::string& _listed)
		{
			if (_listed.size() < 16 || !_file.exists())
				return false;

			const auto number = [&_listed](const size_t _at, const size_t _length)
			{
				return juce::String(_listed.substr(_at, _length)).getIntValue();
			};

			const juce::Time built(number(0, 4), number(5, 2) - 1, number(8, 2), number(11, 2), number(14, 2), 0, 0, true);

			return _file.getLastModificationTime() < built - juce::RelativeTime::hours(4);
		}
	}

	PluginManager::PluginManager(const bool _loadSettings)
		: m_credentials(_loadSettings)
		, m_installPaths(_loadSettings)
		, m_installedPlugins(_loadSettings)
	{
	}

	PluginManager::~PluginManager()
	{
		*m_alive = false;
	}

	// Both paths ask through here, so the setting cannot hold for one of them and not the other.
	CredentialStore PluginManager::credentialsForFetch() const
	{
		auto credentials = m_credentials;

		// Sending it anyway is how a machine with the team login turned off still reported
		// "Team login: Login rejected": the setting reached the window and nothing else.
		if (!m_installPaths.hasTeamLogin())
			credentials.clear(Channel::Internal, {}, false);

		return credentials;
	}

	bool PluginManager::refresh(std::string& _error)
	{
		Catalog catalog;

		const auto success = fetchAll(catalog, credentialsForFetch(), _error);

		m_selfUpdate = evaluateSelfUpdate(catalog);

		// Committed even when something failed: whatever did answer is still worth showing.
		m_catalog = std::move(catalog);

		return success;
	}

	void PluginManager::refreshAsync(ResultCallback _callback)
	{
		const auto generation = ++m_refreshGeneration;

		auto callback = std::make_shared<ResultCallback>(std::move(_callback));
		auto alive = m_alive;

		// The worker gets its own copy of the logins and builds a catalog of its own, so a login made
		// while it runs cannot be read half written, and nothing here is touched until the answer is
		// back on the message thread.
		juce::Thread::launch([this, alive, generation, callback, credentials = credentialsForFetch()]
		{
			auto catalog = std::make_shared<Catalog>();
			std::string error;

			const auto success = fetchAll(*catalog, credentials, error);

			// Here rather than on the message thread: this is the one that may ask the server.
			auto selfUpdate = std::make_shared<std::optional<Asset>>(evaluateSelfUpdate(*catalog));

			juce::MessageManager::callAsync([this, alive, generation, callback, catalog, selfUpdate, success, error]
			{
				if (!*alive || generation != m_refreshGeneration)
					return;

				m_catalog = std::move(*catalog);
				m_selfUpdate = std::move(*selfUpdate);

				if (*callback)
					(*callback)(success, error);
			});
		});
	}

	bool PluginManager::fetchAll(Catalog& _catalog, const CredentialStore& _credentials, std::string& _error)
	{
		// The first failure is the one reported; a later one would hide it.
		const auto fail = [&_error](const std::string& _message)
		{
			if (_error.empty())
				_error = _message;
		};

		_error.clear();

		// Products first: the protected channels are listed per product, so which folders there are to
		// ask for is only known once the lists are in.
		std::vector<Product> products;
		std::string error;		// cleared by every call that fails, so it never carries over

		if (CatalogSource::fetchPublicProducts(products, error))
			_catalog.setProducts(std::move(products));
		else
			fail(error);

		const auto team = _credentials.get(Channel::Internal, {});

		// The products that only exist internally are listed behind the team login, so they are only
		// asked for once there is one.
		if (team.valid())
		{
			std::vector<Product> internalProducts;

			if (CatalogSource::fetchInternalProducts(team, internalProducts, error))
				_catalog.addProducts(internalProducts);
			else
				fail("Team login: " + error);
		}

		std::vector<Asset> assets;

		if (CatalogSource::fetchPublic(assets, error))
			_catalog.addAssets(assets);
		else
			fail(error);

		// A protected channel is only contacted with a login for it: the team login opens every
		// product's internal folder, a donator login only the folder of its own product.
		std::vector<std::string> knownSlugs;

		for (const auto& product : _catalog.getProducts())
		{
			if (product.slug.empty())
				continue;

			knownSlugs.push_back(product.slug);

			for (const auto channel : {Channel::Internal, Channel::Donators})
			{
				const auto credentials = _credentials.get(channel, product.slug);

				if (!credentials.valid())
					continue;

				std::vector<Asset> channelAssets;
				bool hasProductFile = false;

				// A product without a folder in this channel is not worth a message.
				if (!CatalogSource::fetchChannel(product.slug, channel, credentials, channelAssets, error,
					&hasProductFile))
					continue;

				_catalog.addAssets(channelAssets);

				Product described;

				if (hasProductFile && CatalogSource::fetchChannelProduct(product.slug, channel, credentials, described))
					_catalog.setChannelFiles(product.slug, channel, std::move(described.files));
			}
		}

		// A product that has never been published is in no product list, so nothing above knows it
		// exists. Its folder does, and the builds in it are named after it - which is enough to put
		// it in the catalog from a donator login alone.
		for (const auto& slug : _credentials.getSlugs(Channel::Donators))
		{
			if (std::find(knownSlugs.begin(), knownSlugs.end(), slug) != knownSlugs.end())
				continue;

			std::vector<Asset> assets;
			bool hasProductFile = false;

			const auto login = _credentials.get(Channel::Donators, slug);

			if (!CatalogSource::fetchChannel(slug, Channel::Donators, login, assets, error, &hasProductFile))
			{
				continue;
			}

			if (assets.empty())
				continue;

			_catalog.addAssets(assets);

			// The base product, not one of its variants: an FX or console build is the same name with
			// something on the end, so the shortest one is the plugin itself.
			auto name = assets.front().product;

			for (const auto& asset : assets)
			{
				if (asset.product.size() < name.size())
					name = asset.product;
			}

			Product product;
			product.name = name;
			product.description = "Downloaded with the donator login for " + slug;

			// The folder may say what it holds, which beats the above: a name somebody chose, what the
			// hardware is, a picture, what sort of product it is and the hardware's maker, name and year.
			// Only those are taken from it - the slug is the folder that was opened and the product is
			// read off the builds that are in it, so a mistake in that file cannot leave a row that
			// matches none of them.
			Product described;

			if (hasProductFile && CatalogSource::fetchChannelProduct(slug, Channel::Donators, login, described))
			{
				_catalog.setChannelFiles(slug, Channel::Donators, described.files);

				if (!described.name.empty())
					product.name = described.name;

				if (!described.description.empty())
					product.description = described.description;

				product.thumbnail = described.thumbnail;
				product.category = described.category;
				product.manufacturer = described.manufacturer;
				product.originalName = described.originalName;
				product.year = described.year;
			}

			product.slug = slug;
			product.product = name;

			_catalog.addProducts({product});
		}

		return _error.empty();
	}

	PluginManager::ChangelogRequest PluginManager::changelogRequestFor(const std::string& _product) const
	{
		ChangelogRequest request;

		const auto* product = m_catalog.findOwningProduct(_product);

		request.name = product ? product->name : _product;
		request.slug = product ? product->slug : std::string();

		// The logins a refresh would send, so a team login that is turned off is not sent here either.
		const auto credentials = credentialsForFetch();

		request.internal = credentials.get(Channel::Internal, {});
		request.donators = credentials.get(Channel::Donators, request.slug);

		// Only for a product that was published: the public changelog says nothing about any other, and
		// its framework changes would be passed off as the product's own.
		request.published = m_catalog.isPublished(_product);

		if (!request.internal.valid())
			request.newestPublished = m_catalog.getNewestPublishedVersion();

		return request;
	}

	bool PluginManager::fetchChangelog(const ChangelogRequest& _request, changelogLib::Changelog& _changelog, std::string& _error)
	{
		const auto parsed = [&](const std::string& _text)
		{
			if (_changelog.parse(_text))
				return true;

			_error = "The changelog is empty.";
			return false;
		};

		std::string text;

		// A folder without one is an older build, or a product the login does not open there: the next
		// place is asked rather than the answer being that there is none. With both logins both folders
		// are asked and the newer changelog wins, internal on a tie: a build can go to donators without
		// going to internal first, and the internal folder then still holds the changelog of an older one.
		if (!_request.slug.empty())
		{
			// The deploy writes the newest version first, as "Version 2.2.28".
			const auto newestVersion = [](const std::string& _text) -> uint64_t
			{
				const auto at = _text.find("Version ");
				if (at == std::string::npos)
					return 0;
				const auto end = _text.find_first_of("\r\n", at);
				const auto length = end == std::string::npos ? std::string::npos : end - at - 8;
				return changelogLib::Changelog::versionToInt(_text.substr(at + 8, length));
			};

			uint64_t newest = 0;

			for (const auto& [channel, login] : {std::make_pair(Channel::Internal, _request.internal),
			                                     std::make_pair(Channel::Donators, _request.donators)})
			{
				if (!login.valid())
					continue;

				std::string folderText, error;

				if (!CatalogSource::fetchChangelog(_request.slug, channel, login, folderText, error))
					continue;

				const auto version = newestVersion(folderText);

				if (text.empty() || version > newest)
				{
					text = std::move(folderText);
					newest = version;
				}
			}

			if (!text.empty())
				return parsed(text);
		}

		if (!_request.published)
		{
			_error = "No changelog has been published for " + _request.name + " yet.";
			return false;
		}

		if (!HttpClient::fetchString(urls().publicChangelog, text, _error))
		{
			_error = "Could not read the changelog: " + _error;
			return false;
		}

		if (!parsed(text))
			return false;

		if (!_request.newestPublished.empty())
			_changelog.dropNewerThan(_request.newestPublished);

		return true;
	}

	bool PluginManager::fetchChangelog(const std::string& _product, changelogLib::Changelog& _changelog, std::string& _error) const
	{
		return fetchChangelog(changelogRequestFor(_product), _changelog, _error);
	}

	void PluginManager::fetchChangelogAsync(const std::string& _product, ChangelogCallback _callback) const
	{
		auto callback = std::make_shared<ChangelogCallback>(std::move(_callback));
		auto alive = m_alive;

		juce::Thread::launch([alive, callback, request = changelogRequestFor(_product)]
		{
			changelogLib::Changelog changelog;
			std::string error;

			const auto success = fetchChangelog(request, changelog, error);

			juce::MessageManager::callAsync([alive, callback, success, changelog = std::move(changelog), error]
			{
				if (*alive && *callback)
					(*callback)(success, changelog, error);
			});
		});
	}

	bool PluginManager::verifyLogin(const Channel _channel, const std::string& _slug, const Credentials& _credentials,
	                                std::string& _error)
	{
		// What the login unlocks is what checks it: the internal product list for the team login, the
		// product's own folder for a donator login.
		if (_channel == Channel::Internal)
		{
			std::vector<Product> products;
			return CatalogSource::fetchInternalProducts(_credentials, products, _error);
		}

		std::vector<Asset> assets;

		if (!CatalogSource::fetchChannel(_slug, _channel, _credentials, assets, _error))
			return false;

		// A product that is not published reaches the catalog through the builds in its folder, so an
		// empty folder puts nothing there. Storing the login anyway would close this dialog on a list
		// that did not change, which is what a login that did not work looks like.
		if (assets.empty())
		{
			_error = "There is no build in " + CatalogSource::channelUrl(_slug, _channel) + " yet.\n\n"
				"The folder is there and the login was accepted - nothing has been uploaded into it.";
			return false;
		}

		return true;
	}

	bool PluginManager::login(const Channel _channel, const std::string& _slug, const Credentials& _credentials,
	                          std::string& _error)
	{
		if (!verifyLogin(_channel, _slug, _credentials, _error))
			return false;

		m_credentials.set(_channel, _slug, _credentials);

		// Logging in IS turning it on, and the two disagreeing is what made a machine with the
		// setting off go on sending a login it had been told not to use. Written down, because a
		// login that is remembered past this run has to be usable in the next one.
		if (_channel == Channel::Internal && !m_installPaths.hasTeamLogin())
		{
			m_installPaths.setTeamLogin(true);
			m_installPaths.save();
		}

		return true;
	}

	void PluginManager::loginAsync(const Channel _channel, const std::string& _slug, const Credentials& _credentials,
	                               ResultCallback _callback)
	{
		auto callback = std::make_shared<ResultCallback>(std::move(_callback));
		auto alive = m_alive;

		juce::Thread::launch([this, alive, callback, _channel, _slug, _credentials]
		{
			std::string error;

			const auto success = verifyLogin(_channel, _slug, _credentials, error);

			juce::MessageManager::callAsync([this, alive, callback, _channel, _slug, _credentials, success, error]
			{
				if (!*alive)
					return;

				// Stored here rather than on the worker: the store belongs to the message thread.
				if (success)
					m_credentials.set(_channel, _slug, _credentials);

				if (*callback)
					(*callback)(success, error);
			});
		});
	}

	void PluginManager::logout(const Channel _channel, const std::string& _slug)
	{
		m_credentials.clear(_channel, _slug);

		// The other half of login(): nothing is left behind to be sent and refused later.
		if (_channel == Channel::Internal && m_installPaths.hasTeamLogin())
		{
			m_installPaths.setTeamLogin(false);
			m_installPaths.save();
		}
	}

	void PluginManager::setCredentials(const Channel _channel, const std::string& _slug, const Credentials& _credentials)
	{
		m_credentials.set(_channel, _slug, _credentials, false);

		// For this run only, like the login itself: a login handed over on a command line is meant
		// to be used now, and must not need a setting turned on first - but it does not change what
		// the machine does tomorrow.
		if (_channel == Channel::Internal)
			m_installPaths.setTeamLogin(true);
	}

	Credentials PluginManager::credentialsFor(const Asset& _asset) const
	{
		if (_asset.channel == Channel::Public)
			return {};

		// A donator login opens a folder, and the build's own address says which one. The product it
		// is named after can have a slug of its own: a test console is a product of its own in the
		// team's list, but its builds sit in the folder of its plugin, whose login is the only one
		// that opens them.
		if (_asset.channel == Channel::Donators && _asset.url.find("/builds/") != std::string::npos)
			return m_credentials.get(_asset.channel, _asset.url.substr(0, _asset.url.rfind('/') + 1));

		const auto* product = m_catalog.findOwningProduct(_asset.product);

		return m_credentials.get(_asset.channel, product ? product->slug : std::string());
	}

	Credentials PluginManager::loginForUrl(const std::string& _url, const Credentials& _internal, const Credentials& _donators)
	{
		const auto onServer = [&_url](const std::string& _prefix)
		{
			return _url.size() > _prefix.size() && _url.compare(0, _prefix.size(), _prefix) == 0;
		};

		if (!onServer(urls().serverBuilds) && !onServer(urls().previousServerBuilds))
			return {};

		return _url.find("/internal/") != std::string::npos ? _internal : _donators;
	}

	Credentials PluginManager::credentialsForThumbnail(const Product& _product) const
	{
		// The published products keep their pictures on the website, which gets no login at all.
		return loginForUrl(_product.thumbnail, m_credentials.get(Channel::Internal, {}),
			m_credentials.get(Channel::Donators, _product.slug));
	}

	PluginManager::FilesRequest PluginManager::filesRequestFor(const Asset& _asset) const
	{
		FilesRequest request;

		const auto* product = m_catalog.findOwningProduct(_asset.product);
		const auto slug = product ? product->slug : std::string();

		request.product = product ? product->product : _asset.product;

		if (product)
			request.files = product->files;

		// The folder the build came from says what goes with it, which is the newest word on it: a
		// device branch can bring files before any product list knows about them.
		if (const auto* channelFiles = m_catalog.getChannelFiles(slug, _asset.channel))
		{
			if (_asset.channel != Channel::Public && !channelFiles->empty())
				request.files = *channelFiles;
		}

		request.internal = m_credentials.get(Channel::Internal, {});
		request.donators = m_credentials.get(Channel::Donators, slug);

		return request;
	}

	namespace
	{
		// A path beneath _root and nothing outside it: the list comes from a server, and a "../../" in
		// it must not be able to write anywhere else on the disk.
		juce::File inside(const juce::File& _root, const std::string& _path)
		{
			const auto folder = juce::String(_path).replaceCharacter('\\', '/');

			if (folder.isEmpty() || folder.startsWithChar('/') || folder.containsChar(':'))
				return {};

			juce::StringArray parts;
			parts.addTokens(folder, "/", {});
			parts.removeEmptyStrings();

			if (parts.isEmpty() || parts.contains("..") || parts.contains("."))
				return {};

			return _root.getChildFile(parts.joinIntoString("/"));
		}

		// The last part of the path, without what follows it: a query string is not part of a name.
		std::string fileNameOf(const std::string& _url)
		{
			auto path = juce::String(_url).upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
			const auto name = juce::URL::removeEscapeChars(path.fromLastOccurrenceOf("/", false, false));

			if (name.isEmpty() || name == "." || name == ".." || name.containsAnyOf("/\\:"))
				return {};

			return name.toStdString();
		}
	}

	bool PluginManager::installAdditionalFiles(const FilesRequest& _request, std::string& _message)
	{
		_message.clear();

		const auto& files = _request.files;

		if (files.empty())
			return true;

		const auto root = juce::File(juce::String(PlatformUtils::dataFolder(_request.product)));

		size_t arrived = 0;
		size_t failed = 0;
		std::string firstError;

		const auto fail = [&](const std::string& _what)
		{
			Log::write("Additional file for " + _request.product + ": " + _what);

			if (firstError.empty())
				firstError = _what;

			++failed;
		};

		for (const auto& section : files)
		{
			const auto folder = inside(root, section.folder);

			if (folder == juce::File())
			{
				fail("\"" + section.folder + "\" is not a folder of the product's own");
				continue;
			}

			for (const auto& download : section.files)
			{
				const auto& url = download.url;

				// What "path" says, or the name the URL ends in - also beneath a folder the path names.
				auto path = download.path;

				if (path.empty() || path.back() == '/' || path.back() == '\\')
				{
					const auto name = fileNameOf(url);

					if (name.empty())
					{
						fail(url + " does not end in a file name");
						continue;
					}

					path += name;
				}

				const auto target = inside(folder, path);

				if (target == juce::File())
				{
					fail("\"" + path + "\" is not a path inside " + section.folder);
					continue;
				}

				const auto login = loginForUrl(url, _request.internal, _request.donators);

				// Kept unless the server's copy is newer. One it cannot date is kept as well: fetching
				// it again on every install would overwrite what the user did with it for nothing.
				if (target.existsAsFile())
				{
					int64_t modified = 0;
					std::string error;

					if (!HttpClient::fetchLastModified(url, modified, error, login) ||
						modified <= target.getLastModificationTime().toMilliseconds() / 1000)
					{
						continue;
					}
				}

				std::string error;

				if (!target.getParentDirectory().createDirectory())
				{
					fail("Could not create " + target.getParentDirectory().getFullPathName().toStdString());
					continue;
				}

				// Beside it first, so that a download that breaks off does not leave half a file in place
				// of a whole one.
				const auto partial = target.getSiblingFile(target.getFileName() + ".tus-download");

				if (!HttpClient::downloadFile(url, partial.getFullPathName().toStdString(), error, {}, {}, login))
				{
					partial.deleteFile();
					fail(path + ": " + error);
					continue;
				}

				if (!partial.moveFileTo(target))
				{
					partial.deleteFile();
					fail("Could not write " + target.getFullPathName().toStdString());
					continue;
				}

				Log::write("Additional file " + url + " -> " + target.getFullPathName().toStdString() + ": ok");
				++arrived;
			}
		}

		if (failed)
		{
			_message = std::to_string(failed) + (failed == 1 ? " additional file" : " additional files") +
				" could not be installed: " + firstError;
			return false;
		}

		if (arrived)
			_message = std::to_string(arrived) + (arrived == 1 ? " additional file installed" : " additional files installed");

		return true;
	}

	bool PluginManager::installAdditionalFiles(const Asset& _asset, std::string& _message) const
	{
		return installAdditionalFiles(filesRequestFor(_asset), _message);
	}

	void PluginManager::installAdditionalFilesAsync(const Asset& _asset, ResultCallback _callback) const
	{
		auto callback = std::make_shared<ResultCallback>(std::move(_callback));
		auto alive = m_alive;

		juce::Thread::launch([alive, callback, request = filesRequestFor(_asset)]
		{
			std::string message;

			const auto success = installAdditionalFiles(request, message);

			juce::MessageManager::callAsync([alive, callback, success, message]
			{
				if (*alive && *callback)
					(*callback)(success, message);
			});
		});
	}

	std::string PluginManager::owningProduct(const std::string& _product) const
	{
		// An FX build is its own product in the file names but belongs to the plugin it extends, so
		// it follows that product's location rather than having one of its own.
		const auto* product = m_catalog.findOwningProduct(_product);

		return product ? product->product : _product;
	}

	std::string PluginManager::configuredPathFor(const std::string& _product, const PluginFormat _format) const
	{
		const auto owner = owningProduct(_product);
		const auto path = m_installPaths.get(owner, _format);

		// A plugin goes straight into the location for its format, because that is the folder its
		// hosts scan and one in a folder of its own would not be found.
		//
		// A program is the other way round. Nothing scans for them, they all want the same single
		// location, and what they unpack is not named after them: two of the test consoles carry a
		// virusTestConsole and the same pair of start .bat files, and every archive brings its own
		// README and LICENSE. One folder for all of them means each install quietly writes over what
		// the last one put there, so each gets a folder named after itself.
		if (_format != PluginFormat::Standalone || path.empty())
			return path;

		// Except where somebody pointed this one product somewhere by hand, which means that folder
		// and not one below it.
		if (m_installPaths.isCustom(owner, _format))
			return path;

		// Named after the product rather than the one it belongs to: a plugin may bring more than one
		// program, and those would be back in a folder together.
		return juce::File(juce::String(path)).getChildFile(juce::String(_product))
			.getFullPathName().toStdString();
	}

	std::string PluginManager::locateInstalled(const std::string& _product, const PluginFormat _format) const
	{
		// Where it would be installed, then the standard locations for the format, and everything
		// under each of them.
		//
		// Under, because a folder of the maker's name inside the format's folder - VST3/The Usual
		// Suspects/ - is how a good many people keep their plugins, and every one of those was
		// invisible to this and offered as a download (BUG-10456).
		//
		// The standard locations are searched even when the location was pointed somewhere by hand,
		// which is the case that most needs it: the build that was installed before is still in the
		// standard folder, and it is the one an upgrade is being offered for.
		std::vector<std::string> roots;

		const auto configured = configuredPathFor(_product, _format);

		if (!configured.empty())
			roots.push_back(configured);

		for (const auto& dir : PlatformUtils::getSearchPaths(_format))
		{
			if (dir != configured)
				roots.push_back(dir);
		}

		// The folder an install would write to is the first one asked about, so a copy there is the
		// copy this is about whatever else is lying around.
		for (const auto& root : roots)
		{
			for (const auto& folder : foldersUnder(root))
			{
				if (!m_installedPlugins.installedFile(_product, _format, folder).empty())
					return folder;
			}
		}

		return {};
	}

	std::string PluginManager::pathFor(const std::string& _product, const PluginFormat _format) const
	{
		const auto owner = owningProduct(_product);
		const auto configured = configuredPathFor(_product, _format);

		// Where the plugin was found, not where this was: an effect build has no location of its own
		// and belongs in the same folder as the plugin it extends, which is the whole reason it asks
		// under that name. Its own only when the plugin is not installed at all.
		auto found = locateInstalled(owner, _format);

		if (found.empty() && owner != _product)
			found = locateInstalled(_product, _format);

		if (found.empty() || found == configured)
			return configured;

		// Somebody said where this goes, so that is where it goes. The copy found elsewhere is still
		// what the row reports, and leaving it where it is beats overriding a location they chose.
		//
		// Unless it is inside the folder they named, which is not somewhere else at all - it is the
		// same place said more precisely, and installing into the folder above would leave them with
		// the plugin twice.
		if ((m_installPaths.isCustom(_format) || m_installPaths.isCustom(owner, _format))
			&& !juce::File(juce::String(found)).isAChildOf(juce::File(juce::String(configured))))
			return configured;

		// On top of what is there. Unless that folder can be neither written nor asked for, which is a
		// system folder on Linux - see ElevatedTask::isSupported - and then ours goes where it can go
		// and the one that is there is left alone.
		if (ElevatedTask::isSupported() || juce::File(juce::String(found)).hasWriteAccess())
			return found;

		return configured;
	}

	std::string PluginManager::installPathFor(const Asset& _asset) const
	{
		return pathFor(_asset.product, PlatformUtils::formatFromString(_asset.format));
	}

	std::string PluginManager::getInstallPath(const std::string& _product, const std::string& _format) const
	{
		return pathFor(_product, PlatformUtils::formatFromString(_format));
	}

	void PluginManager::setInstallPath(const std::string& _product, const std::string& _format, const std::string& _path)
	{
		m_installPaths.set(owningProduct(_product), PlatformUtils::formatFromString(_format), _path);
		m_installPaths.save();
	}

	PluginManager::Removal PluginManager::removalFor(const std::string& _product, const PluginFormat _format) const
	{
		// From where it actually is, which is not always where an install of it would go - see
		// locateInstalled. Where it would go when there is nothing to remove, so that the failure
		// names the folder the user is looking at.
		const auto found = locateInstalled(_product, _format);

		return {_product, _format, found.empty() ? pathFor(_product, _format) : found};
	}

	bool PluginManager::uninstall(const std::string& _product, const std::string& _format, std::string& _message)
	{
		std::string failure;

		// bool, not auto: front() of a vector<bool> is a proxy into the temporary, gone by the next line.
		const bool gone = removeAll({removalFor(_product, PlatformUtils::formatFromString(_format))}, failure).front();

		_message = gone ? _product + " " + _format + " removed" : failure;

		return gone;
	}

	std::vector<bool> PluginManager::uninstallMany(const std::vector<std::pair<std::string, std::string>>& _items,
	                                             std::string& _failure)
	{
		std::vector<Removal> removals;

		for (const auto& [product, format] : _items)
			removals.push_back(removalFor(product, PlatformUtils::formatFromString(format)));

		return removeAll(removals, _failure);
	}

	std::vector<bool> PluginManager::removeAll(const std::vector<Removal>& _removals, std::string& _failure)
	{
		std::vector<bool> gone(_removals.size(), false);
		std::vector<std::string> needRights;
		std::vector<size_t> fileCounts(_removals.size(), 0);
		std::vector<std::string> plugins(_removals.size());
		std::vector<std::pair<std::string, PluginFormat>> going;

		for (const auto& r : _removals)
			going.emplace_back(r.product, r.format);

		for (size_t i = 0; i < _removals.size(); ++i)
		{
			const auto& r = _removals[i];

			// What that install wrote, less anything another one still claims - one that stays, not one
			// going in this same run. Empty for an install from before this was written down, and then
			// only the plugin itself goes.
			const auto files = m_installedPlugins.filesToRemove(r.product, r.format, r.dir, going);
			fileCounts[i] = files.size();

			std::string message;

			if (!PluginInstaller::uninstall(r.product, m_catalog.dataProductOf(r.product), r.format, r.dir, files,
				message, &needRights, &plugins[i]))
			{
				Log::write("Uninstall " + r.product + " " + PlatformUtils::formatToString(r.format) + " from " + r.dir + ": " + message);

				if (_failure.empty())
					_failure = message;

				continue;
			}

			gone[i] = true;
		}

		std::string rightsError;

		if (!needRights.empty())
		{
			std::vector<bool> done;
			ElevatedTask::removeMany(needRights, done, rightsError);
		}

		for (size_t i = 0; i < _removals.size(); ++i)
		{
			if (!gone[i])
				continue;

			const auto& r = _removals[i];
			const auto format = PlatformUtils::formatToString(r.format);

			// The plugin decides, as it always did. Not the record: a changelog the effect build still
			// claims is kept on purpose, and through it the record would look installed for ever.
			if (juce::File(juce::String(plugins[i])).exists())
			{
				gone[i] = false;

				const auto message = rightsError.empty()
					? "Could not remove " + r.product + " " + format + " from " + r.dir + "."
					: rightsError;

				Log::write("Uninstall " + r.product + " " + format + " from " + r.dir + ": " + message);

				if (_failure.empty())
					_failure = message;

				continue;
			}

			Log::write("Uninstall " + r.product + " " + format + " from " + r.dir + ": ok, " +
				std::to_string(fileCounts[i]) + " file(s)");

			forget(r.product, r.format, r.dir);
		}

		return gone;
	}

	void PluginManager::forget(const std::string& _product, const PluginFormat _format, const std::string& _dir)
	{
		m_installedPlugins.remove(_product, _format);
		m_installedPlugins.save();

		// The folder was made for this one program, so it goes when the last of it does. Only when
		// empty, and only when it is that folder: the location a format installs to belongs to
		// whoever set it, and a plugin shares it with every other plugin.
		if (_format != PluginFormat::Standalone)
			return;

		const auto dir = juce::File(juce::String(_dir));

		if (dir.getFileName() == juce::String(_product) &&
			dir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0)
		{
			dir.deleteFile();
		}
	}

	// One failure does not stop the rest: the reason a format cannot be removed - a host holding it
	// open - is usually about that one format and not about the others.
	size_t PluginManager::uninstallAll(const std::string& _product, const std::vector<std::string>& _formats,
	                                   std::string& _message)
	{
		std::vector<std::pair<std::string, std::string>> items;

		for (const auto& format : _formats)
			items.emplace_back(_product, format);

		std::string failure;

		const auto results = uninstallMany(items, failure);
		const auto removed = static_cast<size_t>(std::count(results.begin(), results.end(), true));

		_message = failure.empty()
			? "Removed " + std::to_string(removed) + (removed == 1 ? " format" : " formats")
			: failure;

		return removed;
	}

	// Asked of every record rather than remembered at the moment a location changes: a copy left
	// behind stays left behind across restarts, and one left by a version of this program that did
	// not look is found just the same.
	std::vector<InstalledPlugins::Entry> PluginManager::getStrayInstalls() const
	{
		std::vector<InstalledPlugins::Entry> strays;

		for (const auto& entry : m_installedPlugins.getEntries())
		{
			const auto format = PlatformUtils::formatFromString(entry.format);

			if (format == PluginFormat::Count)
				continue;

			// Compared as folders, which is what the question is. Doing it by file name would ask it of
			// the name the product would have, and a program does not have that name.
			const auto dir = juce::File(juce::String(entry.path)).getParentDirectory();

			// Where it would go now. The same place means nothing was moved; no place at all means a
			// location somebody emptied on purpose, and what is on disk is as left behind as it gets.
			if (juce::File(juce::String(pathFor(entry.product, format))) == dir)
				continue;

			// Gone from there by other means is not something to offer to remove.
			if (!m_installedPlugins.installedFile(entry.product, format,
				dir.getFullPathName().toStdString()).empty())
				strays.push_back(entry);
		}

		return strays;
	}

	bool PluginManager::removeStray(const InstalledPlugins::Entry& _entry, std::string& _message)
	{
		const auto format = PlatformUtils::formatFromString(_entry.format);

		if (format == PluginFormat::Count)
		{
			_message = "Unknown format " + _entry.format;
			return false;
		}

		const auto dir = juce::File(juce::String(_entry.path)).getParentDirectory().getFullPathName().toStdString();

		std::string failure;

		const bool gone = removeAll({{_entry.product, format, dir}}, failure).front();

		_message = gone ? _entry.product + " " + _entry.format + " removed" : failure;

		return gone;
	}

	bool PluginManager::isInstalled(const std::string& _product, const std::string& _format) const
	{
		return !locateInstalled(_product, PlatformUtils::formatFromString(_format)).empty();
	}

	std::string PluginManager::getInstalledVersion(const std::string& _product, const std::string& _format) const
	{
		const auto format = PlatformUtils::formatFromString(_format);
		const auto dir = locateInstalled(_product, format);

		if (dir.empty())
			return {};

		return m_installedPlugins.getVersion(_product, format, dir);
	}

	std::optional<Asset> PluginManager::evaluateSelfUpdate(const Catalog& _catalog) const
	{
		// A bigger number is an update whatever the dates say.
		if (const auto* newer = SelfUpdate::find(_catalog))
			return *newer;

		// The same number may still be a different build. The listing is what suggests it and the
		// server is what settles it - one request, and only for the one file this would offer.
		const auto* same = SelfUpdate::findSameVersion(_catalog);

		if (same && isNewerBuild(*same) && confirmNewerBuild(*same))
			return *same;

		return {};
	}

	void PluginManager::recordSelfUpdate(const Asset& _asset)
	{
		int64_t modified = 0;
		std::string error;

		HttpClient::fetchLastModified(_asset.url, modified, error, credentialsFor(_asset));

		m_installPaths.setSelfBuild(_asset.listed, modified);
		m_installPaths.save();
	}

	bool PluginManager::isNewerBuild(const Asset& _asset) const
	{
		// A public release says what it is in its version number, and that is the thing to compare.
		if (_asset.channel == Channel::Public || _asset.listed.empty())
			return false;

		// This program is not in installed.json - that is keyed by the folder a format goes to, and
		// this one lives wherever it was put - so its own build is noted in the settings instead.
		if (isAppProduct(_asset.product))
		{
			const auto& listed = m_installPaths.getSelfListed();

			if (!listed.empty())
				return _asset.listed > listed;

			// Only an update writes it down, and the first install is always one by hand. Without this
			// a copy unpacked from an archive was never offered the next build of its own version. The
			// binary rather than the bundle around it on macOS: that is the file whose date the archive
			// kept.
			return isClearlyOlderThan(juce::File::getSpecialLocation(juce::File::currentExecutableFile), _asset.listed);
		}

		const auto format = PlatformUtils::formatFromString(_asset.format);

		// The record belonging to the copy that is installed, or to the folder one would go to when
		// there is none: a record whose files have gone still says which build it was, and this is not
		// the question of whether anything is installed - see decideInstallAction, which asks that one
		// first.
		const auto found = locateInstalled(_asset.product, format);
		const auto* entry = m_installedPlugins.findEntry(_asset.product, format,
			found.empty() ? pathFor(_asset.product, format) : found);

		// Nothing installed, or installed before dates were written down. Either way there is nothing
		// to be newer than.
		if (!entry || entry->listed.empty())
			return false;

		// Fixed width, biggest unit first: one string against the other is the whole comparison, and
		// it never has to know which zone the server keeps its clock in.
		return _asset.listed > entry->listed;
	}

	bool PluginManager::confirmNewerBuild(const Asset& _asset) const
	{
		int64_t installed = 0;

		if (isAppProduct(_asset.product))
		{
			installed = m_installPaths.getSelfModified();
		}
		else
		{
			const auto format = PlatformUtils::formatFromString(_asset.format);
			const auto found = locateInstalled(_asset.product, format);
			const auto* entry = m_installedPlugins.findEntry(_asset.product, format,
				found.empty() ? pathFor(_asset.product, format) : found);

			if (entry)
				installed = entry->modified;
		}

		// Nothing to check against, so what the listing said stands.
		if (!installed)
			return true;

		int64_t modified = 0;
		std::string error;

		if (!HttpClient::fetchLastModified(_asset.url, modified, error, credentialsFor(_asset)))
			return true;

		return modified > installed;
	}

	bool PluginManager::hasNewerBuild(const std::string& _product, const std::string& _format,
	                                  const std::string& _version) const
	{
		const auto* asset = m_catalog.find(_product, _format, _version,
		                                   PlatformUtils::getCurrentOS(), PlatformUtils::getCurrentArch());

		return asset && isNewerBuild(*asset);
	}

	bool PluginManager::isOutdated(const std::string& _product, const std::string& _format,
	                               const std::string& _installedVersion, const std::string& _offered) const
	{
		if (_offered.empty())
			return false;

		if (!_installedVersion.empty())
		{
			const auto difference = compareVersions(_installedVersion, _offered);
			return difference < 0 || (difference == 0 && hasNewerBuild(_product, _format, _offered));
		}

		// Nothing says which version it is: a plugin unpacked by hand, of a format that carries no
		// version. The date of its file is what is left to go on.
		const auto* asset = m_catalog.find(_product, _format, _offered, PlatformUtils::getCurrentOS(),
		                                   PlatformUtils::getCurrentArch());

		if (!asset)
			return false;

		const auto dir = locateInstalled(_product, PlatformUtils::formatFromString(_format));

		if (dir.empty())
			return false;

		return isClearlyOlderThan(juce::File(juce::String(PlatformUtils::installedFilePath(_product,
			PlatformUtils::formatFromString(_format), dir))), asset->listed);
	}

	bool PluginManager::hasUpdate(const std::string& _product) const
	{
		const auto os = PlatformUtils::getCurrentOS();
		const auto arch = PlatformUtils::getCurrentArch();
		const auto fx = m_catalog.fxProductOf(_product);

		for (const auto& format : m_catalog.getFormats(_product, os, arch))
		{
			const auto versions = m_catalog.getVersions(_product, format, os, arch);

			if (versions.empty())
				continue;

			// The effect build goes by the plugin's newest version, as its column does.
			for (const auto& product : {_product, fx})
			{
				if (product.empty() || !isInstalled(product, format))
					continue;

				if (isOutdated(product, format, getInstalledVersion(product, format), versions.front()))
					return true;
			}
		}

		return false;
	}

	InstallAction PluginManager::installAction(const std::string& _product, const std::string& _format, const bool _installed,
	                                           const std::string& _installedVersion, const std::string& _offered) const
	{
		const auto action = decideInstallAction(_installed, _installedVersion, _offered);

		if (action == InstallAction::Reinstall && isOutdated(_product, _format, _installedVersion, _offered))
			return InstallAction::Upgrade;

		return action;
	}

	bool PluginManager::finishPending(ElevatedBatch& _batch, std::string& _error)
	{
		if (_batch.empty())
			return true;

		std::vector<bool> done;

		const auto all = ElevatedTask::copyIntoMany(_batch.getJobs(), done, _error);

		// Per copy, not all or nothing: a plugin a host had open stops that one landing and none of
		// the others, and the five that did land are installed whatever happened to the sixth.
		const auto& records = _batch.getRecords();

		for (size_t i = 0; i < records.size() && i < done.size(); ++i)
		{
			const auto& asset = records[i].asset;

			// The line the install itself could not write, having only unpacked: this is where the
			// files were copied into place, or a declined prompt kept them out.
			Log::write("Install " + asset.product + " " + asset.format + " " + asset.version + " -> " +
				_batch.getJobs()[i].targetDir + (done[i] ? ": ok" : ": " + _error));

			if (done[i])
				recordInstall(asset, records[i].files);
		}

		_batch.clear();

		return all;
	}

	void PluginManager::recordInstall(const Asset& _asset, std::vector<std::string> _files)
	{
		// What the server says about the file this came from, asked once here rather than guessed at
		// later. One request on top of having just downloaded an archive, and it is what lets a
		// later check tell a rebuilt version from the one that is installed. A server that will not
		// say leaves it at zero, and then the listing date is all there is - which is still better
		// than a version number that never changes.
		int64_t modified = 0;

		if (_asset.channel != Channel::Public)
		{
			std::string error;
			HttpClient::fetchLastModified(_asset.url, modified, error, credentialsFor(_asset));
		}

		m_installedPlugins.set(_asset, installPathFor(_asset), _files, modified);
		m_installedPlugins.save();

		// Somewhere to put the ROM. A plugin makes this folder itself, but not until it is first
		// started, and between installing it and running it there is nowhere obvious to put one -
		// which is the order somebody doing this for the first time actually works in.
		//
		// Plugins only. A program makes its own arrangements and never reads this folder, so one made
		// here is a folder nothing will ever open - which is what they turned out to be:
		//
		// * a test console finds its ROM beside its own executable or in the working directory,
		//   those being the only two paths RomLoader starts with
		// * the bridge server has romsPath and pluginsPath in its own config, defaulting under
		//   Documents/The Usual Suspects/dspBridgeServer/, and creates them itself on first run -
		//   note the spelling, which is not the product name, so the one made here was not even in
		//   the right place
		// * a standalone app builds its own the first time it runs, exactly as a plugin does
		//
		// An effect build still makes the plugin's, which is the one it reads.
		if (PlatformUtils::formatFromString(_asset.format) == PluginFormat::Standalone)
			return;

		const auto roms = juce::File(juce::String(PlatformUtils::romFolder(m_catalog.dataProductOf(_asset.product))));

		// Not worth failing an install that already worked, so this is only written down.
		if (!roms.isDirectory() && !roms.createDirectory().wasOk())
			Log::write("Could not create " + roms.getFullPathName().toStdString());
	}
}
