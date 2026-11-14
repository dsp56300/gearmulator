#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "asset.h"
#include "catalog.h"
#include "credentials.h"

namespace pluginManager
{
	// A folder to install from without a network (BUG-10543): the archives under the names the server
	// gives them, beside them the pictures and the additional files of their products, and offline.json,
	// which says what the names cannot - which channel a build came from, when it was listed, what a
	// product is called and what it brings. Preparing an offline install writes one, keeping the
	// downloads fills one, and installing from a folder reads one.
	//
	// A folder of archives without offline.json is read as well, which is what one filled by hand from
	// the releases is: the names are enough to install them. Every build then counts as public and is
	// dated by its file.
	class OfflineFolder
	{
	public:
		static constexpr const char* g_manifest = "offline.json";

		// Which login a file needs, by its URL - see PluginManager::loginForUrl.
		using LoginForUrl = std::function<Credentials(const std::string& _url)>;

		// Reads what is there already, so a folder filled in more than one go keeps all of it.
		explicit OfflineFolder(std::string _dir);

		// What _dir holds, as the catalog takes it: a build for every archive whose name says what it is,
		// with a file:/// URL, and the products offline.json describes - and, for builds it does not
		// describe, a product named after them. False when there is no build in there at all.
		static bool read(const std::string& _dir, std::vector<Asset>& _assets, std::vector<Product>& _products,
		                 std::string& _error);

		// Whether _dir is one of these rather than any folder that may hold an archive or two: it has the
		// file this writes. What the program looks for beside itself when it starts.
		static bool hasManifest(const std::string& _dir);

		// Every change to a folder goes through one of these at a time: the downloads kept by two installs
		// that run side by side would otherwise each write offline.json without what the other added.
		static std::mutex& writeLock();

		// The name an archive has on the server, which is the name it keeps in here.
		static std::string archiveName(const Asset& _asset);

		const std::string& getDir() const { return m_dir; }
		std::string archivePath(const Asset& _asset) const;

		// Whether this build is in here already, from the same listing: a second run over a folder
		// fetches only what is new.
		bool hasBuild(const Asset& _asset) const;

		// An archive that is in the folder now, written down with what its name does not say.
		void addBuild(const Asset& _asset);

		// Whether this product is written down with everything it brings, so that keeping a second format
		// of it does not fetch its picture and its files again.
		bool hasProduct(const Product& _product, const std::vector<FileSection>& _files) const;

		// Fetches a product's picture and the files it brings into the folder, and writes the product
		// down with paths into the folder instead of URLs. _files is what its builds bring - see
		// PluginManager::filesRequestFor - which the product list alone does not always say. Each file
		// keeps the date the server gives it, so an install from here replaces only what is older, as
		// an install from the server would. False when anything could not be fetched; _error says what.
		bool addProduct(const Product& _product, const std::vector<FileSection>& _files, const LoginForUrl& _login,
		                std::string& _error);

		bool save(std::string& _error) const;

	private:
		struct Build
		{
			Channel channel = Channel::Public;
			std::string listed;
		};

		std::string m_dir;
		std::map<std::string, Build> m_builds;			// by archive name
		std::map<std::string, Product> m_products;		// by product, with paths relative to the folder
	};
}
