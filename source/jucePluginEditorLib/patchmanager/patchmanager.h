#pragma once

#include "../../jucePluginLib/patchdb/db.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "state.h"

namespace genericUI
{
	class UiObject;
	class Editor;
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
		explicit PatchManager(genericUI::Editor& _editor, Component* _root, const juce::File& _dir);
		~PatchManager() override;

		void timerCallback() override;

		void setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle) const;
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch, pluginLib::patchDB::SearchHandle _fromSearch, uint32_t _indexInSearch);

		auto& getEditor() const { return m_editor; }
		std::shared_ptr<genericUI::UiObject> getTemplate(const std::string& _name) const;

		virtual uint32_t getCurrentPart() const = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch) = 0;
		virtual bool activatePatch(const pluginLib::patchDB::PatchPtr& _patch, uint32_t _part) = 0;

	private:
		genericUI::Editor& m_editor;

		Tree* m_tree = nullptr;
		List* m_list = nullptr;
		Info* m_info = nullptr;

		SearchList* m_searchList = nullptr;
		SearchTree* m_searchTree = nullptr;

		State m_state;
	};
}
