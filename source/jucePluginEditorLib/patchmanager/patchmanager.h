#pragma once

#include "../../jucePluginLib/patchdb/db.h"

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
		explicit PatchManager(genericUI::Editor& _editor, Component* _root, const juce::File& _json);
		~PatchManager() override;

		void timerCallback() override;

		void setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle);
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch);

		auto& getEditor() const { return m_editor; }
		std::shared_ptr<genericUI::UiObject> getTemplate(const std::string& _name) const;

	private:
		genericUI::Editor& m_editor;

		Tree* m_tree = nullptr;
		List* m_list = nullptr;
		Info* m_info = nullptr;

		SearchList* m_searchList = nullptr;
		SearchTree* m_searchTree = nullptr;
	};
}
