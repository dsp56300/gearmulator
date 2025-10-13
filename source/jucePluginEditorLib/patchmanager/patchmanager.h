#pragma once

#include "state.h"
#include "types.h"

#include "baseLib/event.h"

#include "jucePluginLib/patchdb/db.h"

#include "juce_events/juce_events.h"	// juce::Timer

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
}

namespace genericUI
{
	class UiObject;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManagerUi;

	class PatchManager : public pluginLib::patchDB::DB, juce::Timer
	{
	public:
		baseLib::Event<uint32_t, pluginLib::patchDB::PatchKey> onSelectedPatchChanged;

		static constexpr std::initializer_list<GroupType> DefaultGroupTypes{GroupType::Favourites, GroupType::LocalStorage, GroupType::Factory, GroupType::DataSources};

		explicit PatchManager(Editor& _editor, Rml::Element* _rootElement, const std::initializer_list<patchManager::GroupType>& _groupTypes = DefaultGroupTypes);
		~PatchManager() override;

		void timerCallback() override;
		void processDirty(const pluginLib::patchDB::Dirty& _dirty) const override;

		bool setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch);

		bool selectPrevPreset(uint32_t _part);
		bool selectNextPreset(uint32_t _part);

		bool selectPatch(uint32_t _part, const pluginLib::patchDB::DataSource& _ds, uint32_t _program);

		bool addGroupTreeItemForTag(pluginLib::patchDB::TagType _type) const;
		bool addGroupTreeItemForTag(pluginLib::patchDB::TagType _type, const std::string& _name) const;

		void exportPresets(const juce::File& _file, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, const pluginLib::FileType& _fileType) const;
		bool exportPresets(std::vector<pluginLib::patchDB::PatchPtr>&& _patches, const pluginLib::FileType& _fileType) const;

		bool copyPart(uint8_t _target, uint8_t _source, uint64_t _userData = 0);

		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch);

		const State& getState() const { return m_state; }

		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch);
		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchKey& _patch);

		void copyPatchesToLocalStorage(const pluginLib::patchDB::DataSourceNodePtr& _ds, const std::vector<pluginLib::patchDB::PatchPtr>& _patches, int _part);

		uint32_t createSaveMenuEntries(juceRmlUi::Menu& _menu, uint32_t _part, const std::string& _name = "patch", uint64_t _userData = 0);
		uint32_t createSaveMenuEntries(juceRmlUi::Menu& _menu, const std::string& _name = "patch", uint64_t _userData = 0)
		{
			return createSaveMenuEntries(_menu, getCurrentPart(), _name, _userData);
		}

		std::string getTagTypeName(pluginLib::patchDB::TagType _type) const;
		void setTagTypeName(pluginLib::patchDB::TagType _type, const std::string& _name);

		std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromString(const std::string& _text);
		std::vector<pluginLib::patchDB::PatchPtr> getPatchesFromClipboard();
		bool activatePatchFromString(const std::string& _text);
		bool activatePatchFromClipboard();
		std::string toString(const pluginLib::patchDB::PatchPtr& _patch, const pluginLib::FileType& _fileType, pluginLib::ExportType _exportType) const;

		void setCustomSearch(pluginLib::patchDB::SearchHandle _sh) const;

		void bringToFront() const;

		void setUi(std::unique_ptr<PatchManagerUi> _ui);

	private:
		bool selectPatch(uint32_t _part, int _offset);

	public:
		virtual uint32_t getCurrentPart() const = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) = 0;

		virtual bool activatePatch(const std::string& _filename, uint32_t _part);

		std::vector<pluginLib::patchDB::PatchPtr> loadPatchesFromFiles(const juce::StringArray& _files);
		std::vector<pluginLib::patchDB::PatchPtr> loadPatchesFromFiles(const std::vector<std::string>& _files);

		void onLoadFinished() override;

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data) const;

		void onProgramChanged(uint32_t _part);

		void setCurrentPart(uint32_t _part);

	protected:
		void updateStateAsync(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch);

		void startLoaderThread(const juce::File& _migrateFromDir = {}) override;

	private:
		pluginLib::patchDB::SearchHandle getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem);

		State m_state;

		std::unordered_map<pluginLib::patchDB::TagType, std::string> m_tagTypeNames;

		Editor& m_editor;
		std::unique_ptr<PatchManagerUi> m_ui;
	};
}
