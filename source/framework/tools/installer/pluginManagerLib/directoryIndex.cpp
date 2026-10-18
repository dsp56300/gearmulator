#include "directoryIndex.h"

#include <set>

namespace pluginManager
{
	namespace
	{
		// The file name is taken from the link rather than resolving the href against the folder,
		// because the server writes absolute paths and there is no need to care either way.
		std::string fileNameFromHref(const std::string& _href)
		{
			const auto slash = _href.rfind('/');
			return slash == std::string::npos ? _href : _href.substr(slash + 1);
		}

		bool isDigits(const char* _c, const size_t _count)
		{
			for (size_t i = 0; i < _count; ++i)
			{
				if (_c[i] < '0' || _c[i] > '9')
					return false;
			}

			return true;
		}

		// "2026-10-05 18:37" somewhere between this link and the next one, which is the row it is in.
		// Found by its shape rather than by counting columns: the only thing a listing promises is
		// that the date comes after the name it belongs to. A server that prints it some other way
		// leaves this empty, and then the version number decides as it always did.
		std::string dateInRow(const std::string& _html, const size_t _from, const size_t _to)
		{
			constexpr size_t len = 16;		// yyyy-mm-dd hh:mm

			for (auto i = _from; i + len <= _to; ++i)
			{
				const auto* c = _html.data() + i;

				if (isDigits(c, 4) && c[4] == '-' && isDigits(c + 5, 2) && c[7] == '-' && isDigits(c + 8, 2) &&
					c[10] == ' ' && isDigits(c + 11, 2) && c[13] == ':' && isDigits(c + 14, 2))
					return _html.substr(i, len);
			}

			return {};
		}
	}

	std::vector<Asset> parseDirectoryIndex(const std::string& _html, const std::string& _dirUrl, const Channel _channel)
	{
		std::vector<Asset> assets;
		std::set<std::string> seen;

		constexpr char key[] = "href=";
		constexpr size_t keyLen = sizeof(key) - 1;

		size_t pos = 0;

		while (true)
		{
			pos = _html.find(key, pos);

			if (pos == std::string::npos)
				break;

			pos += keyLen;

			if (pos >= _html.size())
				break;

			const auto quote = _html[pos];

			if (quote != '"' && quote != '\'')
				continue;

			const auto end = _html.find(quote, ++pos);

			if (end == std::string::npos)
				break;

			const auto fileName = fileNameFromHref(_html.substr(pos, end - pos));
			pos = end + 1;

			Asset asset;

			// Anything that is not one of our builds is not ours to offer, so it is dropped
			// rather than guessed at: parent folder links, column sort links, stylesheets.
			if (!parseAssetName(fileName, asset))
				continue;

			if (!seen.insert(fileName).second)
				continue;

			asset.url = _dirUrl + fileName;
			asset.channel = _channel;

			// Up to wherever the next link starts, so a date can only be read out of this file's own row.
			const auto nextLink = _html.find(key, pos);
			asset.listed = dateInRow(_html, pos, nextLink == std::string::npos ? _html.size() : nextLink);

			assets.push_back(std::move(asset));
		}

		return assets;
	}
}
