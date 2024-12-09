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

		return _line;
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
	std::set<std::string> localProducts = { "Osirus", "OsTIrus", "Xenia", "Vavra", "NodalRed2x" };

	auto formatHeader = [format](const std::string & _header)
	{
		if (format == Format::Discord)
		{
			return "**" + _header + "**";
		}
		return _header;
	};

	for (auto& itVersion : productPerVersion)
	{
		const auto& version = itVersion.first;
		const auto& products = itVersion.second;

		std::map<std::string, Lines> globals;
		std::map<std::string, Lines> locals;

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

		// write one file per product
		if (locals.size() > 1)
		{
			for (const auto& itProduct : locals)
			{
				const auto& product = itProduct.first;

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

				writeProduct(outFile, product, itProduct.second, needsSpace);

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
	return 0;
}
