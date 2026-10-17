#include "catalogSource.h"

#include "httpClient.h"
#include "config.h"
#include "log.h"

#include "directoryIndex.h"

#include <cctype>

#include "juce_core/juce_core.h"

namespace pluginManager
{
	namespace
	{
		std::vector<Product> parseProducts(const std::string& _json)
		{
			std::vector<Product> products;

			juce::var parsed;

			if (juce::JSON::parse(juce::String(_json), parsed).failed() || !parsed.isObject())
				return products;

			const auto* root = parsed.getDynamicObject();

			if (!root)
				return products;

			// "products" is the trimmed list that holds display data only. The older "plugins" file
			// carried versions and download URLs too, which is what made it go stale, but its name
			// and image fields are the same - so it is still readable until it is replaced.
			auto list = root->getProperty("products");

			if (!list.isArray())
				list = root->getProperty("plugins");

			if (!list.isArray())
				return products;

			for (const auto& entry : *list.getArray())
			{
				const auto* obj = entry.getDynamicObject();

				if (!obj)
					continue;

				Product p;
				p.slug = obj->getProperty("slug").toString().toStdString();
				p.product = obj->getProperty("product").toString().toStdString();

				if (p.product.empty())
					p.product = obj->getProperty("id").toString().toStdString();
				p.name = obj->getProperty("name").toString().toStdString();
				p.description = obj->getProperty("description").toString().toStdString();
				p.thumbnail = obj->getProperty("thumbnail").toString().toStdString();
				p.category = obj->getProperty("category").toString().toStdString();
				p.manufacturer = obj->getProperty("manufacturer").toString().toStdString();
				p.originalName = obj->getProperty("originalName").toString().toStdString();
				p.year = static_cast<int>(obj->getProperty("year"));		// a number, or a string of one

				// "files": [{"folder": "presets", "urls": ["https://...", {"url": "https://...", "path": "a/b.mid"}]}]
				if (const auto files = obj->getProperty("files"); files.isArray())
				{
					for (const auto& section : *files.getArray())
					{
						const auto* s = section.getDynamicObject();

						if (!s)
							continue;

						FileSection fileSection;
						fileSection.folder = s->getProperty("folder").toString().toStdString();

						if (const auto urls = s->getProperty("urls"); urls.isArray())
						{
							for (const auto& entry : *urls.getArray())
							{
								FileSection::Download download;

								if (entry.isString())
								{
									download.url = entry.toString().toStdString();
								}
								else if (const auto* e = entry.getDynamicObject())
								{
									download.url = e->getProperty("url").toString().toStdString();
									download.path = e->getProperty("path").toString().toStdString();
								}

								if (!download.url.empty())
									fileSection.files.push_back(std::move(download));
							}
						}

						if (!fileSection.files.empty())
							p.files.push_back(std::move(fileSection));
					}
				}

				// The folder on our own server is the lowercase product name, so it only has to be
				// spelled out when it differs. Spelled out as empty, it says there is none: a program
				// listed for a row of its own - a test console - ships from the folder of the product it
				// belongs to, and asking for a folder of its own cost a 404 on every refresh.
				if (!obj->hasProperty("slug") && !p.product.empty())
				{
					p.slug = p.product;
					for (auto& c : p.slug)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				}

				if (p.product.empty())
					continue;

				if (p.name.empty())
					p.name = p.product;

				products.push_back(std::move(p));
			}

			return products;
		}

		// What this program wants out of one release, whether that release came from GitHub or from
		// our copy of what GitHub said: the files hanging off it, whose names say what they are.
		void parseReleaseAssets(const juce::var& _release, std::vector<Asset>& _assets)
		{
			const auto* obj = _release.getDynamicObject();

			if (!obj)
				return;

			const auto releaseAssets = obj->getProperty("assets");

			// "2026-09-20T20:08:24Z" in the shape a folder listing prints, "2026-09-20 20:08".
			const auto published = obj->getProperty("published_at").toString();
			const auto listed = published.length() >= 16
				? (published.substring(0, 10) + " " + published.substring(11, 16)).toStdString()
				: std::string();

			if (!releaseAssets.isArray())
				return;

			for (const auto& entry : *releaseAssets.getArray())
			{
				const auto* assetObj = entry.getDynamicObject();

				if (!assetObj)
					continue;

				Asset asset;

				if (!parseAssetName(assetObj->getProperty("name").toString().toStdString(), asset))
					continue;

				asset.url = assetObj->getProperty("browser_download_url").toString().toStdString();
				asset.channel = Channel::Public;
				asset.listed = listed;

				if (!asset.url.empty())
					_assets.push_back(std::move(asset));
			}
		}

		// Our own copy, read first. One file names the released versions and one file per version
		// holds what the API answered for it, so this is a request per release against a server with
		// no rate limit rather than one request against GitHub, which has one.
		//
		// A version whose file is missing is left out rather than failing the lot: the rest of the
		// catalog is still worth having. Finding nothing at all is what says there is no copy here.
		bool fetchCachedReleases(std::vector<Asset>& _assets, std::string& _error)
		{
			std::string index;

			if (!HttpClient::fetchString(urls().releaseIndex(), index, _error))
				return false;

			juce::var versions;

			if (juce::JSON::parse(juce::String(index), versions).failed() || !versions.isArray())
			{
				_error = "Unexpected version list";
				return false;
			}

			for (const auto& version : *versions.getArray())
			{
				const auto name = version.toString().toStdString();

				if (name.empty())
					continue;

				std::string response;
				std::string missing;

				if (!HttpClient::fetchString(urls().cachedRelease(name), response, missing))
					continue;

				juce::var release;

				if (juce::JSON::parse(juce::String(response), release).failed())
					continue;

				parseReleaseAssets(release, _assets);
			}

			if (!_assets.empty())
				return true;

			_error = "No builds in the cache";

			return false;
		}

		// A wrong login, a folder with nothing in it and an unreachable server all fail the request.
		// Saying which it was spares the user from guessing why a channel came back empty - and a 404
		// read as "could not reach the server" sends them looking at their connection, which is the one
		// thing that was working. What a 404 means depends on what was asked for, so the caller says it.
		std::string loginFailureMessage(const std::string& _error, const std::string& _nothingThere)
		{
			if (HttpClient::isAuthError(_error))
				return "Login rejected";

			if (HttpClient::isNotFound(_error))
				return _nothingThere;

			return "Could not reach the server (" + _error + ")";
		}
	}

	std::string CatalogSource::channelUrl(const std::string& _slug, const Channel _channel)
	{
		const char* dir = _channel == Channel::Internal ? "internal/" : "donators/";

		return urls().serverBuilds + folderName(_slug) + "/" + dir;
	}

	std::string CatalogSource::folderName(const std::string& _typed)
	{
		auto name = juce::String(_typed).trim();

		const auto at = name.lastIndexOfIgnoreCase("/builds/");

		if (at >= 0)
			name = name.substring(at + 8);

		name = name.trimCharactersAtStart("/").trimCharactersAtEnd("/");

		for (const auto* tier : {"/donators", "/internal"})
		{
			if (name.endsWithIgnoreCase(tier))
				name = name.dropLastCharacters(juce::String(tier).length());
		}

		return name.toLowerCase().toStdString();
	}

	// Every published build, from our own copy of GitHub's answers when there is one and from
	// GitHub itself when there is not.
	bool CatalogSource::fetchPublic(std::vector<Asset>& _assets, std::string& _error)
	{
		// Collected aside so that a source which gives up half way does not leave the caller with
		// part of a catalog and the other source's builds on top of it.
		std::vector<Asset> assets;

		if (fetchCachedReleases(assets, _error))
		{
			Log::write("Public builds: " + std::to_string(assets.size()) + " from " + urls().releaseCache);

			_assets.insert(_assets.end(), assets.begin(), assets.end());
			_error.clear();
			return true;
		}

		Log::write("Nothing in our own copy (" + _error + "), asking GitHub");

		assets.clear();

		std::string response;

		if (!HttpClient::fetchString(urls().githubReleases, response, _error))
		{
			_error = "Could not reach GitHub: " + _error;
			return false;
		}

		juce::var parsed;

		if (juce::JSON::parse(juce::String(response), parsed).failed() || !parsed.isArray())
		{
			_error = "Unexpected response from GitHub";
			return false;
		}

		// One request returns every release with every file attached to it, so all published
		// versions are known up front and no per-version request is needed.
		for (const auto& release : *parsed.getArray())
			parseReleaseAssets(release, assets);

		_assets.insert(_assets.end(), assets.begin(), assets.end());
		_error.clear();

		return true;
	}

	bool CatalogSource::fetchChangelog(const std::string& _slug, const Channel _channel, const Credentials& _credentials,
	                                   std::string& _text, std::string& _error)
	{
		return HttpClient::fetchString(channelUrl(_slug, _channel) + "changelog.txt", _text, _error, _credentials);
	}

	bool CatalogSource::fetchChannel(const std::string& _slug, const Channel _channel, const Credentials& _credentials,
	                                 std::vector<Asset>& _assets, std::string& _error, bool* _hasProductFile)
	{
		if (!_credentials.valid())
		{
			_error = "No login for this channel";
			return false;
		}

		const auto url = channelUrl(_slug, _channel);

		std::string response;

		if (!HttpClient::fetchString(url, response, _error, _credentials))
		{
			_error = loginFailureMessage(_error, "There is nothing in " + url + ".\n\nEither no build has "
				"been uploaded for \"" + _slug + "\" yet, or that is not the name the server knows it by.");
			return false;
		}

		_assets = parseDirectoryIndex(response, url, _channel);

		if (_hasProductFile)
			*_hasProductFile = response.find("products.json") != std::string::npos;

		return true;
	}

	bool CatalogSource::fetchChannelProduct(const std::string& _slug, const Channel _channel,
	                                        const Credentials& _credentials, Product& _product)
	{
		std::string response;
		std::string error;

		if (!HttpClient::fetchString(channelUrl(_slug, _channel) + "products.json", response, error, _credentials))
			return false;

		const auto products = parseProducts(response);

		if (products.empty())
			return false;

		_product = products.front();

		return true;
	}

	bool CatalogSource::fetchInternalProducts(const Credentials& _credentials, std::vector<Product>& _products,
	                                          std::string& _error)
	{
		std::string response;

		if (!HttpClient::fetchString(urls().internalProducts(), response, _error, _credentials))
		{
			_error = loginFailureMessage(_error, "The server has no product list.");
			return false;
		}

		_products = parseProducts(response);

		return true;
	}

	bool CatalogSource::fetchPublicProducts(std::vector<Product>& _products, std::string& _error)
	{
		std::string response;

		if (!HttpClient::fetchString(urls().publicProducts, response, _error))
		{
			_error = "Could not download the product list";
			return false;
		}

		_products = parseProducts(response);

		return true;
	}
}
