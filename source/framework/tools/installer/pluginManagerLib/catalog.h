#pragma once

#include <map>
#include <string>
#include <vector>

#include "asset.h"

namespace pluginManager
{
	// Display information for one product. The builds themselves carry no names or descriptions,
	// so this is the only part that has to be maintained by hand - and because it no longer holds
	// versions or download URLs, it does not go stale when a release is published.
	// Files a product brings besides its builds - presets, skins and the like - that go into one folder
	// of its data folder. Which folder means what is the product's business, not this program's.
	struct FileSection
	{
		struct Download
		{
			std::string url;

			// Where in the section's folder: "foo/bar/thefile.xxx". Empty for the last part of the URL's
			// path, and a path ending in "/" puts that name into the folder it names.
			std::string path;
		};

		std::string folder;				// relative to the data folder: "presets", "skins/extra"
		std::vector<Download> files;
	};

	struct Product
	{
		std::string slug;			// directory on our own server, empty for public only products
		std::string product;		// base product id as it appears in build file names
		std::string name;
		std::string description;
		std::string thumbnail;
		std::string category;		// the heading the list shows it under, empty for none
		std::string manufacturer;	// who made the hardware, empty when the list does not say
		std::string originalName;	// what the hardware is called, empty when the list does not say
		int year = 0;				// when the hardware came out, 0 when the list does not say
		std::vector<FileSection> files;
	};

	// The order of the rows within a category: by manufacturer from A to Z, then the oldest hardware
	// first. A product the list gives no manufacturer or no year goes after the ones it does. Equal
	// as far as this goes means equal, so a stable sort leaves those in list order.
	bool sortsBefore(const Product& _a, const Product& _b);

	// Everything the installer knows about what can be downloaded, gathered from all channels.
	class Catalog
	{
	public:
		void addAssets(const std::vector<Asset>& _assets);
		void clear();

		const std::vector<Asset>& getAssets() const { return m_assets; }

		void setProducts(std::vector<Product> _products);

		// What a protected folder's own products.json says comes with the builds in it, read once with
		// the folder rather than once per install. Null for a folder that has none.
		void setChannelFiles(const std::string& _slug, Channel _channel, std::vector<FileSection> _files);
		const std::vector<FileSection>* getChannelFiles(const std::string& _slug, Channel _channel) const;
		const std::vector<Product>& getProducts() const { return m_products; }
		const Product* findProduct(const std::string& _product) const;

		// The product a build belongs to, which for a variant such as an FX build is the base product:
		// the variant ships from the base product's folder, so it is also its login that opens it.
		const Product* findOwningProduct(const std::string& _product) const;

		// Appends the products not yet known. The internal list arrives separately from the public one
		// and may repeat a public product, which must not show up twice. The files a later list names
		// for a known product are added to it, and so is any of its category, manufacturer, original
		// name and year that it had none of.
		void addProducts(const std::vector<Product>& _products);

		// Formats offered for a product on this platform, in the order the UI should show them.
		std::vector<std::string> getFormats(const std::string& _product, const std::string& _os, const std::string& _arch) const;

		// Every version that can be installed here, newest first, so the head of the list is the
		// default selection and the tail is available for a rollback.
		std::vector<std::string> getVersions(const std::string& _product, const std::string& _format,
		                                     const std::string& _os, const std::string& _arch) const;

		// Products that share a base name, e.g. Osirus -> OsirusFX, OsirusTestConsole. Variants are
		// their own products in the build names, so they are found rather than declared.
		std::vector<std::string> getVariants(const std::string& _product) const;

		// The effect build among those: "OsirusFX" next to "Osirus". Empty when there is none.
		//
		// Matched by name rather than taken as the first variant, because a test console is a
		// variant of the same plugin too and is not something to install beside it.
		std::string fxProductOf(const std::string& _product) const;

		// The product whose folder in Documents a build uses: the plugin's own for its effect build,
		// which the plugin works out the same way, and the build's own for everything else.
		std::string dataProductOf(const std::string& _product) const;

		// The widest audience a build is offered to. A published release counts as public even when the
		// same file also sits in a protected channel, so a release is never labelled internal.
		Channel getChannel(const std::string& _product, const std::string& _format, const std::string& _version,
		                   const std::string& _os, const std::string& _arch) const;

		// Prefers the public copy of a build, which downloads without a login, and a zip over the deb
		// and rpm that Linux builds also ship, since a zip installs without a package manager.
		const Asset* find(const std::string& _product, const std::string& _format, const std::string& _version,
		                  const std::string& _os, const std::string& _arch) const;

		// Whether any build of the product, or of a variant of it, was published.
		bool isPublished(const std::string& _product) const;

		// The newest version that was released to the public, of any product. Empty when no
		// published build is known.
		std::string getNewestPublishedVersion() const;

	private:
		// Asset::dataProduct for every asset, again whenever the products or the assets change: which
		// builds are effect builds depends on both.
		void resolveDataProducts();

		std::vector<Asset> m_assets;
		std::vector<Product> m_products;
		std::map<std::pair<std::string, Channel>, std::vector<FileSection>> m_channelFiles;
	};

	// A macOS universal build serves every arch, so it must match whatever the machine reports.
	bool matchesPlatform(const Asset& _asset, const std::string& _os, const std::string& _arch);
}
