#pragma once

#include "../../jucePluginLib/patchdb/db.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "state.h"

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
	class SearchTree;
	class SearchList;
	class Info;
	class List;
	class Tree;

	class PatchManager : public juce::Component, public pluginLib::patchDB::DB, juce::Timer, public juce::DragAndDropContainer
	{
	public:
		explicit PatchManager(Editor& _editor, Component* _root, const juce::File& _dir);
		~PatchManager() override;

		void timerCallback() override;

		void setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle) const;

		bool setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch);

		bool selectPrevPreset(uint32_t _part);
		bool selectNextPreset(uint32_t _part);

		bool selectPatch(uint32_t _part, const pluginLib::patchDB::DataSource& _ds, uint32_t _program);

	private:
		bool selectPatch(uint32_t _part, int _offset);

		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch);

		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch);
		bool setSelectedPatch(uint32_t _part, const pluginLib::patchDB::PatchKey& _patch);

	public:
		auto& getEditor() const { return m_editor; }
		std::shared_ptr<genericUI::UiObject> getTemplate(const std::string& _name) const;

		virtual uint32_t getCurrentPart() const = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch) = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) = 0;

		void onLoadFinished() override;

		void setPerInstanceConfig(const std::vector<uint8_t>& _data);
		void getPerInstanceConfig(std::vector<uint8_t>& _data);

		void onProgramChanged(uint32_t _part);

		void setCurrentPart(uint32_t _part);

	protected:
		void updateStateAsync(uint32_t _part, const pluginLib::patchDB::PatchPtr& _patch);

	private:
		pluginLib::patchDB::SearchHandle getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem);

		Editor& m_editor;

		Tree* m_treeDS = nullptr;
		Tree* m_treeTags = nullptr;
		List* m_list = nullptr;
		Info* m_info = nullptr;

		SearchTree* m_searchTreeDS = nullptr;
		SearchTree* m_searchTreeTags = nullptr;
		SearchList* m_searchList = nullptr;

		State m_state;
	};
}
