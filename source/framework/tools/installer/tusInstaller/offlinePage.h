#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "pluginManagerLib/pluginManager.h"

namespace juce { class FileChooser; }

namespace Rml
{
	class Element;
	class ElementDocument;
	class ElementFormControlInput;
}

namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	// Installing without a network (BUG-10543): from a folder, and preparing one - for this computer or
	// for another one, which may run another system. See OfflineFolder for what such a folder holds.
	//
	// It touches the DOM directly, so it is called the way InstallerDocument calls the other pages: with
	// RmlUi access already taken.
	class OfflinePage
	{
	public:
		// What the list has ticked, which is what this page starts out with the first time it is opened.
		struct Ticked
		{
			std::set<std::string> formats;
			std::set<std::string> products;		// each one a row installs: the plugin, its effect build or both
		};

		OfflinePage(Rml::ElementDocument& _doc, PluginManager& _pluginManager, juceRmlUi::RmlComponent& _rmlComponent);
		~OfflinePage();

		// Installing from a folder, or from the server again with an empty one. The app does that, since
		// the catalog has to be read again.
		void setOnSource(std::function<void(const std::string& _folder)> _onSource) { m_onSource = std::move(_onSource); }

		Rml::Element* getBackButton() const { return m_back; }
		Rml::Element* getPage() const { return m_page; }

		// Every time the page is opened: the catalog and the folder it comes from may have changed.
		void refresh(const Ticked& _ticked);

	private:
		struct System
		{
			std::string os;
			std::string arch;
			std::string label;
		};

		// Every system the catalog has builds for.
		std::vector<System> systems() const;

		// What there is for the system picked: the products and effect builds, and their formats.
		std::vector<std::string> products() const;
		std::vector<std::string> formats() const;

		// "Osirus", "Osirus FX": what a chip says.
		std::string nameOf(const std::string& _product) const;

		PluginManager::OfflineExport request() const;

		// The chips are built again whenever one is clicked: which products and formats there are
		// depends on the system, and a click is rare enough for that not to matter.
		void build();
		void update() const;
		void addChip(Rml::Element& _parent, const std::string& _name, bool _checked, std::function<void()> _onClick);

		void browse();
		void chooseSource();
		void onWrite();

		// From a worker's answer, which arrives outside of any RmlUi handler.
		void withAccess(const std::function<void()>& _func) const;

		Rml::ElementDocument& m_doc;
		PluginManager& m_pluginManager;
		juceRmlUi::RmlComponent& m_rmlComponent;

		Rml::Element* m_page = nullptr;
		Rml::Element* m_back = nullptr;
		Rml::Element* m_source = nullptr;
		Rml::Element* m_online = nullptr;
		Rml::ElementFormControlInput* m_folderInput = nullptr;
		Rml::Element* m_systems = nullptr;
		Rml::Element* m_formatChips = nullptr;
		Rml::Element* m_productChips = nullptr;
		Rml::Element* m_manager = nullptr;
		Rml::Element* m_write = nullptr;
		Rml::Element* m_status = nullptr;
		Rml::Element* m_progress = nullptr;

		std::function<void(const std::string&)> m_onSource;

		std::string m_folder;
		std::string m_os;
		std::string m_arch;
		std::set<std::string> m_products;
		std::set<std::string> m_formats;
		bool m_withManager = true;
		bool m_initialised = false;

		// While a folder is being written: what stops it.
		std::shared_ptr<bool> m_cancel;

		std::unique_ptr<juce::FileChooser> m_chooser;
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
