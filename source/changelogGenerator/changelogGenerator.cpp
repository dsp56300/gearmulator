#include <algorithm>
#include <fstream>
#include <iostream>
#include <ostream>
#include <vector>
#include <functional>

#include "baseLib/commandline.h"

using Lines = std::vector<std::string>;
using LinesPerKey = std::map<std::string, Lines>;

namespace
{
	enum class Format
	{
		Txt,
		Discord
	};

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
		while (_lines.front().empty())
			_lines.erase(_lines.begin());
		while (_lines.back().empty())
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

	uint64_t versionToInt(const std::string& _v)
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

	bool compareVersions(const std::string& _a, const std::string& _b)
	{
		return versionToInt(_a) < versionToInt(_b);
	}

	std::string parseProduct(const std::string& _line)
	{
		// Needs to start with an uppercase letter A-Z
		if (_line.empty() || !std::isupper(_line.front()))
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
	bool writeProduct(std::ofstream& _out, const std::string& _product, const Lines& _lines, const bool _needsSpace)
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
}

int main(const int _argc, char* _argv[])
{
	const baseLib::CommandLine cmdLine(_argc, _argv);

	const auto inFile = cmdLine.get("i");

	if (inFile.empty())
	{
		std::cout << "No input file specified" << '\n';
		return -1;
	}

	auto outPath = cmdLine.get("o");

	if (outPath.empty())
	{
		std::cout << "No output path specified" << '\n';
		return -1;
	}

	if (outPath.back() != '/' && outPath.back() != '\\')
		outPath.push_back('/');

	auto f = cmdLine.get("f");

	auto format = Format::Txt;

	if (!f.empty())
	{
		if (f == "discord")
		{
			format = Format::Discord;
		}
		else
		{
			std::cout << "Unknown format '" << f << "'\n";
			return -1;
		}
	}

	std::ifstream file(inFile);

	if (!file.is_open())
	{
		std::cout << "Failed to open input file '" << inFile << "'" << '\n';
		return -1;
	}

	Lines allLines;

	while (true)
	{
		std::string line;
		std::getline(file, line);
		if (file.eof())
			break;
		trim(line);
		allLines.push_back(line);
	}

	if (allLines.empty())
	{
		std::cout << "No lines read from input file" << '\n';
		return -1;
	}

	// group by version
	const auto linesPerVersion = groupBy(allLines, parseVersion);

	// group by product
	std::map<std::string, LinesPerKey> productPerVersion;

	for (const auto& it : linesPerVersion)
	{
		const auto& version = it.first;
		const auto& l = it.second;
		const auto linesPerProduct = groupBy(l, parseProduct);
		productPerVersion.insert({ version, linesPerProduct });
	}

	// multiple products might have been specified via /, i.e. Osirus/OsTIrus, add them to two individual products
	for (auto& itVersion : productPerVersion)
	{
		const auto& version = itVersion.first;
		auto& productPerLines = itVersion.second;

		for (auto itProduct = productPerLines.begin(); itProduct != productPerLines.end();)
		{
			const auto& product = itProduct->first;
			const auto& lines = itProduct->second;
			const auto pos = product.find('/');
			
			if (pos == std::string::npos)
			{
				++itProduct;
				continue;
			}

			const auto productA = product.substr(0, pos);
			const auto productB = product.substr(pos + 1);
			auto& linesA = productPerVersion[version][productA];
			linesA.insert(linesA.end(), lines.begin(), lines.end());
			auto& linesB = productPerVersion[version][productB];
			linesB.insert(linesB.end(), lines.begin(), lines.end());
			itProduct = productPerLines.erase(itProduct);
		}
	}

	// adjust spacing for all lines
	for (auto& itVersion : productPerVersion)
	{
		for (auto& itProduct : itVersion.second)
			fixSpacing(itProduct.second);
	}

	// create individual files per version and product
	std::set<std::string> globalProducts = { "DSP", "Framework", "Patch Manager" };
	std::set<std::string> localProducts;

	// find all local products
	for (const auto& it : productPerVersion)
	{
		const auto& products = it.second;

		for (const auto& [productName, productData] : products)
		{
			if (productName.empty())
				continue;

			if (globalProducts.find(productName) == globalProducts.end())
				localProducts.insert(productName);
		}
	}

	auto formatHeader = [format](const std::string & _header)
	{
		if (format == Format::Discord)
		{
			return "**" + _header + "**";
		}
		return _header;
	};

	using Globals = std::map<std::string, Lines>;
	using Locals = std::map<std::string, Lines>;

	// to create a file per product with all versions included
	std::map<std::string, std::vector<std::pair<std::string, std::pair<Globals, Lines>>>> outputs;

	for (auto& itVersion : productPerVersion)
	{
		const auto& version = itVersion.first;
		const auto& products = itVersion.second;

		Globals globals;
		Locals locals;

		for (const auto& itProduct : products)
		{
			const auto& product = itProduct.first;

			if (globalProducts.find(product) != globalProducts.end())
				globals.insert({ product, itProduct.second });
			else
				locals.insert({ product, itProduct.second });
		}

		if (!globals.empty())
		{
			for (const auto& localProduct : localProducts)
			{
				if (locals.find(localProduct) == locals.end())
					locals.insert({ localProduct, {} });
			}
		}

		for (const auto& localName : localProducts)
		{
			auto it = locals.find(localName);

			if ( globals.empty() && it == locals.end())
				continue;

			outputs[localName].emplace_back(version, std::pair(globals, it == locals.end() ? Lines() : it->second));
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
				{
					std::cout << "Failed to create output file '" << outName << '\n';
					return -1;
				}

				if (!product.empty())
					outFile << formatHeader(product + " Version " + version) << '\n';
				else	
					outFile << formatHeader("Version " + version) << '\n';
				outFile << '\n';

				if (format == Format::Discord)
					outFile << "```\n";

				bool needsSpace = false;

				for (const auto& global : globals)
					needsSpace |= writeProduct(outFile, global.first, global.second, needsSpace);

				writeProduct(outFile, product, lines, needsSpace);

				if (format == Format::Discord)
					outFile << "```\n";
			}
		}

		// write one file for all products
		const auto outName = outPath + fixFilename(version + ".txt");
		std::ofstream outFile(outName);

		if (!outFile.is_open())
		{
			std::cout << "Failed to create output file '" << outName << '\n';
			return -1;
		}
		outFile << formatHeader("Version " + version) << '\n';
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
	for (auto& itOutput : outputs)
	{
		const auto& product = itOutput.first;

		auto versions = itOutput.second;

		std::sort(versions.begin(), versions.end(), [&](const auto& a, const auto& b)
		{
			return compareVersions(b.first, a.first);
		});

		const auto outName = outPath + fixFilename("changelog_" + product + ".txt");
		std::ofstream outFile(outName);
		if (!outFile.is_open())
		{
			std::cout << "Failed to create output file '" << outName << '\n';
			return -1;
		}

		bool needsSpace = false;

		for (auto& it : versions)
		{
			const auto& version = it.first;
			const auto& globals = it.second.first;
			const auto& localLines = it.second.second;

			if (needsSpace)
				outFile << '\n';

			outFile << formatHeader("Version " + version) << "\n\n";

			if (format == Format::Discord)
				outFile << "```\n";

			for (const auto& global : globals)
				needsSpace |= writeProduct(outFile, global.first, global.second, needsSpace);

			if (!localLines.empty())
				needsSpace |= writeProduct(outFile, product, localLines, needsSpace);

			if (format == Format::Discord)
				outFile << "```\n";
		}
	}

	return 0;
}
