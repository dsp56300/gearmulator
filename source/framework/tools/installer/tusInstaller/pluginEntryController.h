#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "installTask.h"

#include "pluginManagerLib/pluginManager.h"

namespace Rml { class Element; class Event; }
namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	// One plugin row: the formats available for it here, a version to install per format, the install
	// button that walks them.
	class PluginEntryController
	{
	public:
		// Called from inside an RmlUi event handler; see InstallerDocument for what that rules out.
		using ProductRequest = std::function<void(const Product& _product)>;

		// What the All column acts on: every ticked format the action applies to. There is no
		// downgrading a whole row - which version that would even be is a per-format question.
		enum class Bulk
		{
			Install,	// what is not installed
			Upgrade,	// what is installed and older than the version picked for it, ticked or not
			Remove		// what is installed
		};

		// _allFormats is every format any plugin is offered in here, not only this one's: a column is
		// laid out for each of them so that the formats of one row sit under those of the next, and
		// the ones this plugin does not come in are left empty.
		//
		// Not const: a finished install is written down, so that its version is known next time.
		PluginEntryController(Product _product, PluginManager& _pluginManager, Rml::Element* _element,
		                      juceRmlUi::RmlComponent& _rmlComponent, ProductRequest _onShowDetails,
		                      std::vector<std::string> _allFormats);
		~PluginEntryController();

		const std::string& getProductId() const { return m_product.product; }

		// Reads what is on disk again for every format in this row. The detail page installs and
		// removes things too, and this row went on showing what was true when it was last built.
		void refreshInstalled();

		void updateThumbnail();
		void setThumbnailFilename(std::string _filename);
		const std::string& getThumbnailFilename() const { return m_thumbnailFilename; }

		// Ticks or unticks one format here, for the toggles that do it to every plugin at once.
		void setFormatSelected(const std::string& _format, bool _selected);

		// Every format in this row at once, for the button that does it to every row. A row that is
		// offered nothing here counts as fully selected, so it cannot hold the button on "Select all"
		// for ever.
		void setAllFormatsSelected(bool _selected);
		bool areAllFormatsSelected() const;

		// True unless this row offers that format and it is unticked: a format this plugin does not
		// come in is not deselected, it is simply not here.
		bool isFormatSelected(const std::string& _format) const;

		// Whether the row has that format at all. Needed beside the question above, which answers
		// true for one it does not have - right for "is this row holding it back", wrong for
		// "is it ticked anywhere".
		bool offersFormat(const std::string& _format) const;

		// What has been ticked here, and whether the effect build goes along. A row is thrown away
		// and built again whenever the catalog is read, so what the user chose has to be readable
		// off it before it goes and settable on the one that takes its place.
		std::vector<std::string> getSelectedFormats() const;
		bool isFxSelected() const { return m_fxSelected; }
		void setFxSelected(bool _selected);

		// How much this row would do, which is what the buttons above the list count up.
		size_t countFor(const Bulk _what) const { return formatsFor(_what).size(); }

		// The same action, started from up there rather than from this row's own button. _onDone is
		// called when the row is through - including when it had nothing to do or was already busy,
		// so that whoever walks the rows one after another is never left waiting on one.
		// _shared is the run's own collection of copies that need administrator rights. Given one, this
		// row adds to it and leaves the asking to whoever owns it - that is what makes a walk over six
		// products one prompt instead of six.
		void startBulk(Bulk _what, std::function<void()> _onDone, PluginInstaller::Pending _shared = {});

		// What is on disk for every format of this row, read again and shown. For the end of a run
		// whose copies were made somewhere else.
		void refreshAll();

		// Called whenever anything about this row changes, so those buttons can count again.
		void setOnChanged(std::function<void()> _onChanged) { m_onChanged = std::move(_onChanged); }

	private:
		// One format column: what is installed, which version is picked, and the button that acts on
		// the two of them.
		struct FormatRow
		{
			std::string format;
			bool selected = false;		// nothing is ticked until somebody ticks it (EMU-248)
			bool installed = false;
			std::string installedVersion;		// empty when nothing is installed or it does not say

			// The effect build of the same format, which is a product of its own on the server and
			// goes into the same folder. Only looked at while the FX chip is ticked.
			bool fxInstalled = false;
			std::string fxInstalledVersion;
			Rml::Element* column = nullptr;
			Rml::Element* label = nullptr;
			Rml::Element* installedLabel = nullptr;
			Rml::Element* versionLabel = nullptr;
			Rml::Element* actionButton = nullptr;
			std::vector<std::string> versionList;
			size_t versionIndex = 0;

			const std::string& selectedVersion() const;
		};

		// One thing to install: which of this row's formats, and whether it is the effect build of
		// that format rather than the plugin itself.
		struct Job
		{
			size_t format = 0;
			bool fx = false;
		};

		// The row itself opens the detail page, so everything on it that does something of its own
		// keeps its click to itself.
		static void claimClick(Rml::Element* _element, const std::function<void(Rml::Event&)>& _handler);

		std::vector<size_t> formatsFor(Bulk _what) const;

		// Whether the effect builds are part of what this row is about: there has to be one, and
		// the chip has to be ticked.
		bool wantsFx() const { return !m_fxProduct.empty() && m_fxSelected; }
		bool hasFxBuild(const FormatRow& _row) const;

		// What the button in a column offers, which is about the plugin and, while the FX chip is
		// ticked, about its effect build as well.
		InstallAction actionFor(const FormatRow& _row) const;

		void setupFxToggle();

		void populate();
		void addFormatRows();
		void addAllColumn();
		void openVersionMenu(size_t _index, Rml::Event& _event);

		// _short is what fits in a column: the channel of a build that is not public is a letter
		// there and a word in the menu.
		std::string versionText(const FormatRow& _row, size_t _index, bool _short = false) const;
		void setVersionLabel(const FormatRow& _row) const;
		// Whether this format is on the disk where it would be installed to, and what the column says
		// about it - including which of install, reinstall, upgrade or downgrade its button offers.
		void refreshInstalledState(FormatRow& _row) const;
		void updateRow(const FormatRow& _row) const;

		// The three buttons follow what is ticked: an action with nothing to act on is not offered.
		void updateAllColumn();

		void onBulk(Bulk _what);

		// One format on its own, which is what the button in its column does.
		void onFormatAction(size_t _index);
		void onFormatRemove(size_t _index);

		// Installs these formats one after the other, which is the same walk whether one button
		// started it or the All column did. _skipUpToDate is what tells the two apart: a button
		// pressed on one column means that column whatever state it is in, and the All column
		// means whatever of it there is something to do about.
		void startInstalls(const std::vector<size_t>& _formats, bool _skipUpToDate = false);

		void onCancelClicked();
		void installNext();

		// The end of a run: the copies it collected are made behind one prompt, what landed is
		// written down, and the rows show what is on disk rather than what was asked for.
		void finishElevated();
		void finishQueue();
		void setProgress(float _progress);
		void showProgress(bool _show);
		void setStatus(const std::string& _text);

		Product m_product;
		PluginManager& m_pluginManager;
		const Catalog& m_catalog;
		std::string m_thumbnailFilename;

		// Every format on offer here, in the order every row lays its columns out in.
		std::vector<std::string> m_allFormats;

		Rml::Element* m_element = nullptr;
		juceRmlUi::RmlComponent& m_rmlComponent;
		ProductRequest m_onShowDetails;

		InstallTask m_installer;

		// The copies this run is not allowed to make itself, collected while it walks the formats and
		// made in one go at the end. Four formats into a system folder is one prompt rather than four.
		PluginInstaller::Pending m_elevated;
		bool m_ownsElevated = true;				// false while a run over several rows is collecting them
		std::vector<FormatRow> m_formats;

		// The confirmation before removing a whole row is answered after this row may have been
		// replaced by a refresh, so what it calls back into is dropped when that happened.
		std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

		// Who is waiting for this row to be through with an install queue, and who wants to know
		// that something here changed.
		std::function<void()> m_onQueueDone;
		std::function<void()> m_onChanged;

		// What is still to be installed after the current download finishes, so that ticking three
		// formats installs three of them rather than only the first.
		std::vector<Job> m_pending;

		// The effect build of this plugin, if the archives hold one: "OsirusFX" next to "Osirus".
		// Empty when there is none, and then the chip is not in the row at all.
		std::string m_fxProduct;
		bool m_fxSelected = false;
		Rml::Element* m_fxToggle = nullptr;

		Rml::Element* m_thumbnailImg = nullptr;
		Rml::Element* m_nameElem = nullptr;
		Rml::Element* m_descElem = nullptr;

		// The All column: how much of the row is installed, and the three things that can be done to
		// all of it at once.
		Rml::Element* m_allSummary = nullptr;
		Rml::Element* m_allInstall = nullptr;
		Rml::Element* m_allUpgrade = nullptr;
		Rml::Element* m_allRemove = nullptr;
		Rml::Element* m_progressDiv = nullptr;
		Rml::Element* m_progressBar = nullptr;
		Rml::Element* m_cancelButton = nullptr;
		Rml::Element* m_statusElem = nullptr;
	};
}
