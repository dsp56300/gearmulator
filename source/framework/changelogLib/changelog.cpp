#include "changelog.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace changelogLib
{
	namespace
	{
		using LinesPerKey = std::map<std::string, Lines>;

		const std::set<std::string> g_globalProducts = { "DSP", "Framework", "Patch Manager" };

		std::string& trim(std::string& _line)
		{
			auto needsTrim = [](const char _c) -> bool
			{
				return _c == ' ' || _c == '\t' || _c == '\n' || _c == '\r';
			};

			while (!_line.empty())
			{
				if (needsTrim(_line.back()))
					_line.pop_back();
				else if (needsTrim(_line.front()))
					_line.erase(_line.begin());
				else
					break;
			}
			return _line;
		}

		Lines& trim(Lines& _lines)
		{
			while (!_lines.empty() && _lines.front().empty())
				_lines.erase(_lines.begin());
			while (!_lines.empty() && _lines.back().empty())
				_lines.pop_back();
			return _lines;
		}

		std::string parseVersion(const std::string& _line)
		{
			const auto posA = _line.find('.');
			const auto posB = _line.find('.', posA + 1);

			if (posA == std::string::npos || posB == std::string::npos || posB < posA)
			{
				return {};
			}

			for (size_t i = 0; i < posA; ++i)
			{
				if (!std::isdigit(_line[i]))
					return {};
			}

			for (size_t i = posA + 1; i < posB; ++i)
			{
				if (!std::isdigit(_line[i]))
					return {};
			}

			for (size_t i = posA + 1; i < posB; ++i)
			{
				if (!std::isdigit(_line[i]))
					return {};
			}

			if (_line.back() == ':')
				return _line.substr(0, _line.size() - 1);

			return _line;
		}

		// "Version 2.2.16", as the release notes head a version. Only when that is all the line says, so
		// that a sentence which happens to start with the word is not taken for one.
		std::string parseVersionHeading(const std::string& _line)
		{
			static const std::string prefix = "Version ";

			if (_line.compare(0, prefix.size(), prefix) != 0)
				return parseVersion(_line);

			auto version = _line.substr(prefix.size());

			if (!version.empty() && version.back() == ':')
				version.pop_back();

			size_t dots = 0;

			for (size_t i = 0; i < version.size(); ++i)
			{
				if (version[i] == '.' && i > 0 && i + 1 < version.size() && version[i - 1] != '.')
					++dots;
				else if (!std::isdigit(static_cast<unsigned char>(version[i])))
					return {};
			}

			return dots == 2 ? version : std::string();
		}

		bool compareVersions(const std::string& _a, const std::string& _b)
		{
			return Changelog::versionToInt(_a) < Changelog::versionToInt(_b);
		}

		std::string parseProduct(const std::string& _line)
		{
			// Needs to start with an uppercase letter or a digit - a product name may begin
			// with one (88emuPlayer). Version headers also start with a digit, but they are
			// consumed by the outer grouping before this ever sees them; reject them anyway
			// so the two can never be confused.
			if (_line.empty() || !(std::isupper(_line.front()) || std::isdigit(_line.front())))
				return {};

			if (!parseVersion(_line).empty())
				return {};

			// Needs to have : at the end
			if (_line.back() != ':')
				return {};

			auto res = _line;
			res.pop_back();
			return res;
		}

		LinesPerKey groupBy(const Lines& _lines, const std::function<std::string(const std::string&)>& _eval)
		{
			Lines currentLines;

			std::map<std::string, Lines> linesPerKey;

			std::string currentKey;

			for (const auto& line : _lines)
			{
				const auto key = _eval(line);
				if (!key.empty())
				{
					if (!currentKey.empty())
					{
						linesPerKey.insert({currentKey, trim(currentLines)});
						currentLines.clear();
					}
					currentKey = key;
				}
				else if (!currentKey.empty())
				{
					currentLines.push_back(line);
				}
			}

			if (!currentKey.empty() && !currentLines.empty())
				linesPerKey.insert({ currentKey, trim(currentLines) });

			if (linesPerKey.empty())
				linesPerKey.insert({ "", _lines });
			return linesPerKey;
		}

		Lines& fixSpacing(Lines& _lines)
		{
			size_t spaces = 0;

			for (auto& line : _lines)
			{
				if (line.empty())
					continue;

				if (line.front() == '-')
				{
					// lines either start with "- " or with "- [...] ", adjust spaces accordingly
					auto bracketPos = line.find(']');
					if (bracketPos != std::string::npos)
						spaces = bracketPos + 2;
					else
						spaces = 2;
				}
				else if (spaces)
				{
					for (size_t i=0; i<spaces; ++i)
						line.insert(line.begin(), ' ');
				}
			}
			return _lines;
		}

		std::string fixFilename(const std::string& _filename)
		{
			std::string out;
			for (auto& c : _filename)
			{
				if (c != ':')
					out += c;
			}
			return out;
		}

		bool writeProduct(std::ostream& _out, const std::string& _product, const Lines& _lines, const bool _needsSpace)
		{
			if (_lines.empty())
				return false;

			if (_needsSpace)
				_out << '\n';

			if (!_product.empty())
			{
				_out << _product << ":\n";
				_out << '\n';
			}

			for (const auto& line : _lines)
				_out << line << '\n';

			return true;
		}

		std::string formatHeader(const Changelog::Format _format, const std::string& _header)
		{
			if (_format == Changelog::Format::Discord)
			{
				return "**" + _header + "**";
			}
			return _header;
		}

		// A section and its entries, an entry to a line whatever the file wrapped it at: as text
		// "Framework:" and "- [Fix] ...", as Markdown "### Framework" and "- **Fix** ...".
		//
		// The text goes in as it is written: where it marks something up - `input_play` - that is meant
		// for Markdown, and escaping it would print the backticks instead.
		void writeEntries(std::ostream& _out, const std::string& _section, const Lines& _lines, const bool _markdown)
		{
			if (_lines.empty())
				return;

			if (_markdown)
				_out << "\n### " << _section << "\n\n";
			else
				_out << '\n' << _section << ":\n\n";

			for (const auto& entry : Changelog::getEntries(_lines))
			{
				_out << "- ";

				if (!entry.tag.empty())
					_out << (_markdown ? "**" + entry.tag + "** " : "[" + entry.tag + "] ");

				_out << entry.text << '\n';
			}
		}

		std::string releaseText(const Changelog::Release& _release, const std::string& _product, const bool _markdown)
		{
			std::ostringstream out;

			if (_markdown)
				out << "## " << (_product.empty() ? "Version " + _release.version : _product + " " + _release.version) << '\n';
			else
				out << (_product.empty() ? "Version " + _release.version : _product + " Version " + _release.version) << '\n';

			for (const auto& [section, lines] : _release.global)
				writeEntries(out, section, lines, _markdown);

			writeEntries(out, _product, _release.product, _markdown);

			return out.str();
		}

		// The file changelogGenerator writes for one version of one product, wrapped as doc/changelog.txt is.
		std::string versionFile(const Changelog::Release& _release, const std::string& _product, const Changelog::Format _format)
		{
			std::ostringstream out;

			if (!_product.empty())
				out << formatHeader(_format, _product + " Version " + _release.version) << '\n';
			else
				out << formatHeader(_format, "Version " + _release.version) << '\n';
			out << '\n';

			if (_format == Changelog::Format::Discord)
				out << "```\n";

			bool needsSpace = false;

			for (const auto& global : _release.global)
				needsSpace |= writeProduct(out, global.first, global.second, needsSpace);

			writeProduct(out, _product, _release.product, needsSpace);

			if (_format == Changelog::Format::Discord)
				out << "```\n";

			return out.str();
		}

		std::string normalized(const std::string& _name)
		{
			std::string out;

			for (const auto c : _name)
			{
				if (c != ' ')
					out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}

			return out;
		}
	}

	bool Changelog::isGlobal(const std::string& _section)
	{
		return g_globalProducts.find(_section) != g_globalProducts.end();
	}

	std::vector<Changelog::Entry> Changelog::getEntries(const Lines& _lines)
	{
		std::vector<Entry> entries;

		for (const auto& line : _lines)
		{
			const auto first = line.find_first_not_of(' ');

			if (first == std::string::npos)
				continue;

			auto text = line.substr(first);

			if (text.compare(0, 2, "- ") != 0)
			{
				if (entries.empty())
					entries.push_back({{}, text});
				else
					entries.back().text += " " + text;
				continue;
			}

			Entry entry;
			text = text.substr(2);

			if (!text.empty() && text.front() == '[')
			{
				const auto close = text.find(']');

				if (close != std::string::npos)
				{
					entry.tag = text.substr(1, close - 1);
					text = text.substr(close + 1);

					const auto start = text.find_first_not_of(' ');
					text = start == std::string::npos ? std::string() : text.substr(start);
				}
			}

			entry.text = text;
			entries.push_back(std::move(entry));
		}

		return entries;
	}

	std::string Changelog::toText(const Release& _release, const std::string& _product)
	{
		return releaseText(_release, _product, false);
	}

	std::string Changelog::toMarkdown(const Release& _release, const std::string& _product)
	{
		return releaseText(_release, _product, true);
	}

	uint64_t Changelog::versionToInt(const std::string& _v)
	{
		try
		{
			auto posA = _v.find('.');
			auto posB = _v.find('.', posA + 1);
			auto end = _v.find_first_not_of("0123456789", posB + 1);
			if (posA == std::string::npos || posB == std::string::npos)
				return 0;
			const auto major = std::stoull(_v.substr(0, posA));
			const auto minor = std::stoull(_v.substr(posA + 1, posB - posA - 1));
			const auto patch = std::stoull(_v.substr(posB + 1, end == std::string::npos ? std::string::npos : end - posB - 1));
			return (major << 32) | (minor << 16) | patch;
		}
		catch (const std::invalid_argument& e)
		{
			std::cerr << "Invalid version string '" << _v << "': " << e.what() << '\n';
			return 0;
		}
	}

	bool Changelog::parse(const std::string& _text)
	{
		m_versions.clear();
		m_products.clear();

		Lines allLines;

		std::istringstream in(_text);
		std::string line;

		while (std::getline(in, line))
		{
			trim(line);
			allLines.push_back(line);
		}

		if (allLines.empty())
			return false;

		// group by version
		const auto linesPerVersion = groupBy(allLines, parseVersionHeading);

		// group by product
		for (const auto& it : linesPerVersion)
		{
			const auto& version = it.first;
			const auto& l = it.second;
			const auto linesPerProduct = groupBy(l, parseProduct);
			m_versions.insert({ version, linesPerProduct });
		}

		// multiple products might have been specified via /, i.e. Osirus/OsTIrus, add them to individual
		// products. Any number of them: splitting only the first / left the remainder as one product whose
		// name still contained a /, which is not a legal filename.
		for (auto& itVersion : m_versions)
		{
			const auto& version = itVersion.first;
			auto& productPerLines = itVersion.second;

			for (auto itProduct = productPerLines.begin(); itProduct != productPerLines.end();)
			{
				const auto& product = itProduct->first;
				const auto& lines = itProduct->second;

				if (product.find('/') == std::string::npos)
				{
					++itProduct;
					continue;
				}

				for (size_t begin = 0; begin <= product.size();)
				{
					const auto pos = product.find('/', begin);
					const auto name = product.substr(begin, pos - begin);
					// A heading written "Osirus/", "/Osirus" or "Osirus//OsTIrus" has an empty
					// segment, which would become an output file with no basename at all.
					if (!name.empty())
					{
						auto& dst = m_versions[version][name];
						dst.insert(dst.end(), lines.begin(), lines.end());
					}
					if (pos == std::string::npos)
						break;
					begin = pos + 1;
				}

				itProduct = productPerLines.erase(itProduct);
			}
		}

		// adjust spacing for all lines
		for (auto& itVersion : m_versions)
		{
			for (auto& itProduct : itVersion.second)
				fixSpacing(itProduct.second);
		}

		collectProducts();

		return true;
	}

	void Changelog::dropNewerThan(const std::string& _version)
	{
		const auto newest = versionToInt(_version);

		for (auto it = m_versions.begin(); it != m_versions.end();)
			it = versionToInt(it->first) > newest ? m_versions.erase(it) : std::next(it);

		// A product that only the dropped versions were about is not one this changelog knows.
		collectProducts();
	}

	void Changelog::collectProducts()
	{
		m_products.clear();

		// find all local products
		for (const auto& it : m_versions)
		{
			const auto& products = it.second;

			for (const auto& [productName, productData] : products)
			{
				if (productName.empty())
					continue;

				if (!isGlobal(productName))
					m_products.insert(productName);
			}
		}
	}

	bool Changelog::load(const std::string& _file, std::string& _error)
	{
		std::ifstream file(_file, std::ios::binary);

		if (!file.is_open())
		{
			_error = "Failed to open input file '" + _file + "'";
			return false;
		}

		std::stringstream text;
		text << file.rdbuf();

		if (!parse(text.str()))
		{
			_error = "No lines read from input file";
			return false;
		}

		return true;
	}

	std::string Changelog::findProduct(const std::string& _name) const
	{
		const auto wanted = normalized(_name);

		std::string longestPrefix;

		for (const auto& product : m_products)
		{
			const auto name = normalized(product);

			if (name == wanted)
				return product;

			if (!name.empty() && wanted.compare(0, name.size(), name) == 0 && name.size() > normalized(longestPrefix).size())
				longestPrefix = product;
		}

		return longestPrefix;
	}

	std::vector<Changelog::Release> Changelog::getReleases(const std::string& _product) const
	{
		std::vector<Release> releases;

		for (const auto& [version, sections] : m_versions)
		{
			Release release;
			release.version = version;

			auto ownSection = false;

			for (const auto& [section, lines] : sections)
			{
				if (isGlobal(section))
				{
					release.global.insert({section, lines});
				}
				else if (!_product.empty() && section == _product)
				{
					release.product = lines;
					ownSection = true;
				}
			}

			if (release.global.empty() && !ownSection)
				continue;

			releases.push_back(std::move(release));
		}

		std::sort(releases.begin(), releases.end(), [&](const Release& a, const Release& b)
		{
			return compareVersions(b.version, a.version);
		});

		return releases;
	}

	bool Changelog::writeFiles(const std::string& _outPath, const Format _format, std::string& _error) const
	{
		const auto& outPath = _outPath;
		const auto format = _format;

		const auto failed = [&_error](const std::string& _outName)
		{
			_error = "Failed to create output file '" + _outName;
			return false;
		};

		auto header = [format](const std::string& _header)
		{
			return formatHeader(format, _header);
		};

		using Globals = std::map<std::string, Lines>;
		using Locals = std::map<std::string, Lines>;

		for (auto& itVersion : m_versions)
		{
			const auto& version = itVersion.first;
			const auto& products = itVersion.second;

			Globals globals;
			Locals locals;

			for (const auto& itProduct : products)
			{
				const auto& product = itProduct.first;

				if (isGlobal(product))
					globals.insert({ product, itProduct.second });
				else
					locals.insert({ product, itProduct.second });
			}

			if (!globals.empty())
			{
				for (const auto& localProduct : m_products)
				{
					if (locals.find(localProduct) == locals.end())
						locals.insert({ localProduct, {} });
				}
			}

			// write one file per product/version separated
			if (!locals.empty())
			{
				for (const auto& itProduct : locals)
				{
					const auto& product = itProduct.first;
					const auto& lines = itProduct.second;

					const auto outName = outPath + fixFilename(version + "_" + product + ".txt");
					std::ofstream outFile(outName);

					if (!outFile.is_open())
						return failed(outName);

					outFile << versionFile({version, globals, lines}, product, format);
				}
			}

			// write one file for all products
			const auto outName = outPath + fixFilename(version + ".txt");
			std::ofstream outFile(outName);

			if (!outFile.is_open())
				return failed(outName);

			outFile << header("Version " + version) << '\n';
			outFile << '\n';

			if (format == Format::Discord)
				outFile << "```\n";

			bool needsSpace = false;

			for (const auto& global : globals)
				needsSpace |= writeProduct(outFile, global.first, global.second, needsSpace);

			for (const auto& local : locals)
				needsSpace |= writeProduct(outFile, local.first, local.second, needsSpace);

			if (format == Format::Discord)
				outFile << "```\n";
		}

		// write a file per product with all versions
		for (const auto& product : m_products)
		{
			const auto outName = outPath + fixFilename("changelog_" + product + ".txt");
			std::ofstream outFile(outName);

			if (!outFile.is_open())
				return failed(outName);

			bool needsSpace = false;

			for (const auto& release : getReleases(product))
			{
				if (needsSpace)
					outFile << '\n';

				outFile << header("Version " + release.version) << "\n\n";

				if (format == Format::Discord)
					outFile << "```\n";

				for (const auto& global : release.global)
					needsSpace |= writeProduct(outFile, global.first, global.second, needsSpace);

				if (!release.product.empty())
					needsSpace |= writeProduct(outFile, product, release.product, needsSpace);

				if (format == Format::Discord)
					outFile << "```\n";
			}
		}

		return true;
	}
}
