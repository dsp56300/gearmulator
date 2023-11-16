#pragma once

#include <set>
#include <string>
#include <vector>

#include "juce_gui_basics/juce_gui_basics.h"
#include "juce_cryptography/hashing/juce_MD5.h"

namespace pluginLib
{
	class Controller;
}

namespace jucePluginEditorLib
{
	class Editor;

	struct Patch
	{
		virtual ~Patch() = default;

	    int progNumber = 0;
	    std::string name;
	    std::vector<uint8_t> sysex;
	};

	class PatchBrowser : public juce::FileBrowserListener, juce::TableListBoxModel
	{
	public:
		struct ColumnDefinition
		{
			const char* name = nullptr;
			int id = 0;
			int width = 0;
		};

		using PatchList = std::vector<std::shared_ptr<Patch>>;

		PatchBrowser(const Editor& _editor, pluginLib::Controller& _controller, juce::PropertiesFile& _config, const std::initializer_list<ColumnDefinition>& _columns);
		~PatchBrowser() override;

		bool selectPrevPreset();
		bool selectNextPreset();

		uint32_t load(PatchList& _result, std::set<std::string>* _dedupeChecksums, const std::vector<std::vector<uint8_t>>& _packets);
		bool load(PatchList& _result, std::set<std::string>* _dedupeChecksums, const std::vector<uint8_t>& _data);
		virtual bool loadUnkownData(std::vector<std::vector<uint8_t>>& _result, const std::string& _filename);
		uint32_t loadBankFile(PatchList& _result, std::set<std::string>* _dedupeChecksums, const juce::File& file);

	protected:
		virtual Patch* createPatch() = 0;
		virtual bool initializePatch(Patch& _patch) = 0;
		virtual juce::MD5 getChecksum(Patch& _patch) = 0;
		virtual bool activatePatch(Patch& _patch) = 0;
	public:
		virtual int comparePatches(int _columnId, const Patch& _a, const Patch& _b) const = 0;
	protected:
		virtual std::string getCellText(const Patch& _patch, int _columnId) = 0;
		virtual bool selectPrevNextPreset(int _dir);

		void fillPatchList(const PatchList& _patches);
		void refreshPatchList();
		void onFileSelected(const juce::File& file);

		void fitInParent(juce::Component& _component, const std::string& _parentName) const;

	private:
		// Inherited via FileBrowserListener
	    void selectionChanged() override {}
	    void fileClicked(const juce::File &file, const juce::MouseEvent &e) override;
	    void fileDoubleClicked(const juce::File &file) override {}
	    void browserRootChanged(const juce::File &newRoot) override {}

	    // Inherited via TableListBoxModel
		int getNumRows() override;
		void paintRowBackground(juce::Graphics &, int rowNumber, int width, int height, bool rowIsSelected) override;
		void paintCell(juce::Graphics &, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
		void selectedRowsChanged(int lastRowSelected) override;
		void cellDoubleClicked (int rowNumber, int columnId, const juce::MouseEvent &) override;
	    void sortOrderChanged(int newSortColumnId, bool isForwards) override;

	protected:
		const Editor& m_editor;
		pluginLib::Controller& m_controller;
		juce::PropertiesFile& m_properties;

		juce::WildcardFileFilter m_fileFilter;
	    juce::FileBrowserComponent m_bankList;
	    juce::TextEditor m_search;
	    juce::TableListBox m_patchList;
		juce::ComboBox* m_romBankSelect = nullptr;

		PatchList m_patches;
	    PatchList m_filteredPatches;

		juce::HashMap<juce::String, bool> m_checksums;
		bool m_sendOnSelect = true;
	};
}
