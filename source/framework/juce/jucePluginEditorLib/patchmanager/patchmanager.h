#pragma once

#include "types.h"

#include "jucePluginLib/patchmanager/patchmanager.h"

#include <memory>
#include <vector>

namespace Rml
{
	class Element;
}

namespace juceRmlUi
{
	class Menu;
}

namespace jucePluginEditorLib
{
	class Editor;
	class Processor;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManagerUi;

	// The patch manager of a product. The processor owns it, so it works whether an editor was ever opened or not. An
	// editor shows it while it exists, see attachUi().
	class PatchManager : public pluginLib::patchManager::PatchManager
	{
	public:
		static constexpr std::initializer_list<GroupType> DefaultGroupTypes{GroupType::Favourites, GroupType::MidiBanks, GroupType::LocalStorage, GroupType::Factory, GroupType::DataSources};

		explicit PatchManager(Processor& _processor, const std::initializer_list<GroupType>& _groupTypes = DefaultGroupTypes);
		~PatchManager() override;

		Processor& getProcessor() const { return m_processor; }

		// _editor shows the patch manager in _root until detachUi()
		void attachUi(Editor& _editor, Rml::Element* _root);
		void detachUi();

		// the editor that shows the patch manager, if any
		Editor* getEditor() const { return m_editor; }

		void processPending() override;

		bool addGroupTreeItemForTag(pluginLib::patchDB::TagType _type);
		bool addGroupTreeItemForTag(pluginLib::patchDB::TagType _type, const std::string& _name);

		void exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const pluginLib::FileType& _fileType) const;
		bool exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, const pluginLib::FileType& _fileType) const;

		uint32_t createSaveMenuEntries(juceRmlUi::Menu& _menu, uint32_t _part, const std::string& _name = "patch", uint64_t _userData = 0);
		uint32_t createSaveMenuEntries(juceRmlUi::Menu& _menu, const std::string& _name = "patch", uint64_t _userData = 0)
		{
			return createSaveMenuEntries(_menu, getCurrentPart(), _name, _userData);
		}

		std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromString(const std::string& _text);
		std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromClipboard();
		bool activatePatchFromString(const std::string& _text);
		bool activatePatchFromClipboard();
		std::string toString(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const;

		void setCustomSearch(pluginLib::patchDB::SearchHandle _sh) const;

		void bringToFront() const;

		using pluginLib::patchManager::PatchManager::loadPatchesFromFiles;
		std::vector<pluginLib::patchDB::PatchPtr> loadPatchesFromFiles(const juce::StringArray& _files);

		uint32_t getCurrentPart() const override;
		uint32_t getPartCount() const override;

	protected:
		void startLoaderThread(const juce::File& _migrateFromDir = {}) override;

		void onErrors(const std::vector<std::string>& _errors) const override;

		bool canLoadProgramChangeDirectly(uint32_t _part) const override;

	private:
		Processor& m_processor;
		const std::vector<GroupType> m_groupTypes;

		// tag groups the product wants in the tree, created whenever an editor shows the patch manager
		std::vector<std::pair<pluginLib::patchDB::TagType, std::string>> m_tagGroups;

		Editor* m_editor = nullptr;
		std::unique_ptr<PatchManagerUi> m_ui;
	};
}
