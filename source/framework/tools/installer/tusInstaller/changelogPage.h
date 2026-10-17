#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "changelogLib/changelog.h"

#include "pluginManagerLib/pluginManager.h"

namespace Rml
{
	class Element;
	class ElementDocument;
}

namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	// One product's changelog, a version at a time: the versions down the left, newest first, and on
	// the right what changed in the one picked - what is about this product apart from what every
	// product got, the framework.
	//
	// Where the text comes from is PluginManager::fetchChangelog. Called the way InstallerDocument
	// calls the other pages, with RmlUi access already taken.
	class ChangelogPage
	{
	public:
		ChangelogPage(Rml::ElementDocument& _doc, juceRmlUi::RmlComponent& _rmlComponent);
		~ChangelogPage();

		// Asks for the changelog and shows its newest version once it is here.
		void show(const Product& _product, const PluginManager& _pluginManager);

		Rml::Element* getBackButton() const { return m_back; }
		Rml::Element* getPage() const { return m_page; }

	private:
		void onChangelog(const changelogLib::Changelog& _changelog);
		void select(size_t _index);
		void copy(bool _markdown) const;
		void setStatus(const std::string& _text) const;
		void clear();

		Rml::ElementDocument& m_doc;
		juceRmlUi::RmlComponent& m_rmlComponent;

		Rml::Element* m_page = nullptr;
		Rml::Element* m_back = nullptr;
		Rml::Element* m_name = nullptr;
		Rml::Element* m_status = nullptr;
		Rml::Element* m_versions = nullptr;
		Rml::Element* m_text = nullptr;

		Product m_product;
		std::string m_section;		// what the changelog calls the product, empty when it has no section
		std::vector<changelogLib::Changelog::Release> m_releases;
		std::vector<Rml::Element*> m_versionElements;
		size_t m_selected = 0;

		// Only the answer to the newest request is shown: opening another product's changelog while
		// one is still on its way must not end with the first one on screen.
		uint32_t m_request = 0;
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
