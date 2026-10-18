#pragma once

#include <string>
#include <vector>

#include "asset.h"

namespace pluginManager
{
	// Turns the directory listing of a protected channel folder into assets.
	//
	// The builds on our own server have no index file and no API in front of them - the server
	// simply serves a generated listing of the folder, and every build in it is an <a href>. So
	// the listing is the catalog: collect the links, keep the ones whose file name parses as one
	// of our builds, and ignore the rest (parent folder link, sort links, stylesheets).
	//
	// _dirUrl is the folder the listing came from and must end in '/'; download URLs are built
	// from it plus the file name, which avoids having to resolve whatever form the href takes.
	std::vector<Asset> parseDirectoryIndex(const std::string& _html, const std::string& _dirUrl, Channel _channel);
}
