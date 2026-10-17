#pragma once

#include <string>
#include <vector>

#include "asset.h"
#include "catalog.h"
#include "credentials.h"

namespace pluginManager
{
	// Fetches build listings. Public builds come from the GitHub releases API; internal and donator
	// builds have no API in front of them, so their folder listing on our own server is the index.
	//
	// Every call blocks. PluginManager is what runs them off the message thread for the GUI.
	class CatalogSource
	{
	public:
		static bool fetchPublic(std::vector<Asset>& _assets, std::string& _error);

		// The changelog the build jobs put next to the builds in a protected folder: that product's
		// own, as changelogGenerator writes it.
		static bool fetchChangelog(const std::string& _slug, Channel _channel, const Credentials& _credentials,
		                           std::string& _text, std::string& _error);

		// One product's folder in one protected channel. A wrong password fails with "Login rejected"
		// instead of coming back as an empty list, so the UI can say what went wrong.
		//
		// _hasProductFile says whether the folder holds a products.json - see fetchChannelProduct, which
		// is then worth asking and otherwise only a 404.
		static bool fetchChannel(const std::string& _slug, Channel _channel, const Credentials& _credentials,
		                         std::vector<Asset>& _assets, std::string& _error, bool* _hasProductFile = nullptr);

		// The list of products that only exist internally. It lives behind the team login because
		// the names of unreleased products are exactly what the server's index hides.
		static bool fetchInternalProducts(const Credentials& _credentials, std::vector<Product>& _products,
		                                  std::string& _error);

		// What a channel folder says about the product in it, from a products.json beside the builds.
		// For a plugin that is in no product list the folder is all there is, and this is how that
		// folder gives it a name, a description and a picture instead of the program guessing from
		// build file names. Same shape as the two lists, so the same parser reads it; the first entry
		// is the one used, since a folder holds one product. Having none is the normal case, so
		// failing is not worth a message.
		static bool fetchChannelProduct(const std::string& _slug, Channel _channel,
		                                const Credentials& _credentials, Product& _product);

		// Public product display data, which carries no versions or download URLs.
		static bool fetchPublicProducts(std::vector<Product>& _products, std::string& _error);

		static std::string channelUrl(const std::string& _slug, Channel _channel);

		// The folder name in whatever somebody typed for one.
		//
		// People paste the link from the download post rather than the name in it - and the base and
		// the tier were then put round the whole link a second time (EMU-219). Whatever follows
		// /builds/ is the name, the tier and the slashes the link carries are dropped, and it is
		// lowercase, as every one of our folders is. Anything else comes back as it was, trimmed.
		//
		// Both the request and the stored login go through this, so a login typed either way is
		// checked against the right folder and remembered under the name the catalog asks for.
		static std::string folderName(const std::string& _typed);
	};
}
