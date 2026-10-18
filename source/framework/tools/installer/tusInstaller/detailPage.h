#pragma once

#include <memory>
#include <string>
#include <vector>

#include "installTask.h"

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
	class InstallerDataProvider;

	// One product in full: what the catalog offers for it, where each format goes, and what can be
	// done with it - install, upgrade or remove, per format or all at once.
	//
	// It touches the DOM directly, so it is called the way InstallerDocument calls it: with RmlUi
	// access already taken.
	class DetailPage
	{
	public:
		DetailPage(Rml::ElementDocument& _doc, InstallerDataProvider& _dataProvider,
		           juceRmlUi::RmlComponent& _rmlComponent);
		~DetailPage();

		void show(const Product& _product, PluginManager& _pluginManager);

		Rml::Element* getBackButton() const { return m_back; }
		Rml::Element* getPage() const { return m_page; }

		const std::string& getProductId() const { return m_productId; }
		const std::string& getThumbnailFilename() const { return m_thumbnailFilename; }

		// The thumbnail arrives after the page may already be open.
		void updateThumbnail();

	private:
		struct FormatRow
		{
			std::string format;
			std::string newest;				// the version an install would put there
			std::string installedVersion;	// empty when nothing is installed, or it does not say
			bool installed = false;

			// The effect build of the same format, which the archives ship as a product of its own
			// and which goes into the same folder. fxOffered is whether there is one to fetch at all;
			// what is installed is worth showing either way.
			bool fxOffered = false;
			bool fxInstalled = false;
			std::string fxInstalledVersion;

			Rml::Element* element = nullptr;
			Rml::ElementFormControlInput* path = nullptr;
			Rml::Element* state = nullptr;
			Rml::Element* fxState = nullptr;
			Rml::Element* installButton = nullptr;
		};

		// One thing to install: which of the rows, and whether it is the effect build of that format
		// rather than the plugin itself.
		struct Job
		{
			size_t format = 0;
			bool fx = false;
		};

		void addFormatRow(FormatRow _row);
		void refreshRow(FormatRow& _row) const;
		void refreshAll();

		void setupFxToggle();

		// Whether the effect builds are part of what this page is about: there has to be one, and
		// the chip has to be ticked.
		bool wantsFx() const { return !m_fxProduct.empty() && m_fxSelected; }

		// Whether a row's install and upgrade take the effect build along: while the chip is ticked,
		// and whenever it is installed, as in the list (BUG-10488).
		bool fxIncluded(const FormatRow& _row) const
		{
			return includesFxBuild(_row.fxOffered, wantsFx(), _row.fxInstalled);
		}

		// What the button in a row offers, which is about the plugin and, while the chip is ticked,
		// about its effect build as well.
		InstallAction actionFor(const FormatRow& _row) const;

		// Installs what is queued one after the other, which is how a row fetches the plugin and
		// its effect build without the two of them racing for the same folder.
		void installNext();

		void onInstall(size_t _index);
		void onRemove(size_t _index);
		void onRemoveAll();

		// The product's own folder: what is in it, and the zips taken of it. Separate from the
		// formats above, because nothing up there can put any of it back.
		void refreshBackups();
		void onBackup();
		void onRestore(const std::string& _file);
		void onRevealBackup(const std::string& _file) const;
		void onDeleteBackup(const std::string& _file);
		void onBrowse(size_t _index);
		void setPath(FormatRow& _row, const std::string& _path);

		void setStatus(const std::string& _text) const;

		Rml::ElementDocument& m_doc;
		InstallerDataProvider& m_dataProvider;
		juceRmlUi::RmlComponent& m_rmlComponent;

		Rml::Element* m_page = nullptr;
		Rml::Element* m_back = nullptr;
		Rml::Element* m_name = nullptr;
		Rml::Element* m_description = nullptr;
		Rml::Element* m_meta = nullptr;
		Rml::Element* m_status = nullptr;
		Rml::Element* m_thumbnail = nullptr;
		Rml::Element* m_thumbnailImage = nullptr;
		Rml::Element* m_formats = nullptr;
		Rml::Element* m_removeAll = nullptr;
		Rml::Element* m_data = nullptr;
		Rml::Element* m_dataSize = nullptr;
		Rml::Element* m_backupButton = nullptr;
		Rml::Element* m_dataNote = nullptr;
		Rml::Element* m_backupList = nullptr;

		// A backup or a restore is on a worker thread. Both touch the same folder, so neither may
		// start while the other runs, and the buttons say so rather than looking ready.
		bool m_busy = false;

		PluginManager* m_pluginManager = nullptr;
		Product m_product;
		std::string m_productId;
		std::string m_thumbnailFilename;

		std::vector<FormatRow> m_rows;

		// The effect build of the plugin on show, if the archives hold one: "OsirusFX" next to
		// "Osirus". Empty when there is none, and then the chip is not on the page at all.
		std::string m_fxProduct;
		bool m_fxSelected = false;
		Rml::Element* m_fxToggle = nullptr;

		// What is still to be installed after the current download finishes.
		std::vector<Job> m_pending;

		std::unique_ptr<InstallTask> m_installTask;
		std::unique_ptr<juce::FileChooser> m_chooser;

		// The browser and the install answer after this page may have moved on to another product.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
	};
}
