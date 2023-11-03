#pragma once

#include "../../jucePluginLib/patchdb/db.h"

namespace jucePluginEditorLib::patchManager
{
	class Info;
	class List;
	class Tree;

	class PatchManager : public juce::Component, public pluginLib::patchDB::DB, juce::Timer, public juce::DragAndDropContainer
	{
	public:
		explicit PatchManager(juce::Component* _root, const juce::File& _json);
		~PatchManager() override;

		void timerCallback() override;

		void setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle);
		void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch);

	private:
		Tree* m_tree = nullptr;
		List* m_list = nullptr;
		Info* m_info = nullptr;
	};
}