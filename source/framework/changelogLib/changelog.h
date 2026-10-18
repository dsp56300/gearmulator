#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace changelogLib
{
	using Lines = std::vector<std::string>;

	// Heading -> its lines: "Framework", "Osirus", ...
	using Sections = std::map<std::string, Lines>;

	// doc/changelog.txt, read into versions and the sections of each. What changelogGenerator splits into
	// a file per product and version, and what the Plugin Manager shows per product and version.
	//
	// Reads the file as it is written - "2.2.26:" over each version - and the release notes made from it,
	// which head a version "Version 2.2.26". A heading naming several products, "Osirus/OsTIrus:", is
	// given to each of them.
	class Changelog
	{
	public:
		enum class Format
		{
			Txt,
			Discord
		};

		// One version as one product sees it: the sections about every product, and its own lines.
		struct Release
		{
			std::string version;
			Sections global;
			Lines product;
		};

		// One entry: "- [Fix] text" and the lines that carry it on, joined - the file wraps at a
		// column, whatever shows it wraps where it has to.
		struct Entry
		{
			std::string tag;	// Fix, Imp, New - whatever is in the brackets, empty when there are none
			std::string text;
		};

		// What is about every product rather than one: DSP, Framework, Patch Manager.
		static bool isGlobal(const std::string& _section);

		static std::vector<Entry> getEntries(const Lines& _lines);

		// One release of one product as text: its sections, the shared ones first, and an entry to a
		// line - not wrapped as the file is, so that it can be pasted wherever it is going. _product
		// is the heading of its own section, empty for none.
		static std::string toText(const Release& _release, const std::string& _product);

		// The same as Markdown: a heading per section, a bullet per entry.
		static std::string toMarkdown(const Release& _release, const std::string& _product);

		static uint64_t versionToInt(const std::string& _version);

		// False when there is not a line to read.
		bool parse(const std::string& _text);

		// _error says what went wrong, in the words changelogGenerator has always printed.
		bool load(const std::string& _file, std::string& _error);

		// Leaves out every version after this one: what the next release will bring is written down
		// before it is out.
		void dropNewerThan(const std::string& _version);

		// Every product with a section of its own somewhere, and not one of the global ones.
		const std::set<std::string>& getProducts() const { return m_products; }

		// The name a product has in here, found the way people write it: case and spaces do not
		// matter, so "DSPBridge" finds "DSP Bridge". Failing that, the longest name the product's own
		// starts with, which is how the build called "DSPBridgeServer" finds it too. Empty when it has
		// no section anywhere.
		std::string findProduct(const std::string& _name) const;

		// Newest first. Every version with global sections or a section of the product's own; the
		// product's lines are empty for one with only the former. An empty product gets the global ones.
		std::vector<Release> getReleases(const std::string& _product) const;

		// The files changelogGenerator writes: one per version, one per version and product, and one per
		// product with every version in it. _outPath ends with a separator.
		bool writeFiles(const std::string& _outPath, Format _format, std::string& _error) const;

	private:
		void collectProducts();

		std::map<std::string, Sections> m_versions;
		std::set<std::string> m_products;
	};
}
