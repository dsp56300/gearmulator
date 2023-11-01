#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "../../jucePluginLib/patchdb/db.h"

namespace jucePluginEditorLib::patchManager
{
	class List;
	class Tree;

	class PatchManager : public juce::Component, public pluginLib::patchDB::DB, juce::Timer, public juce::DragAndDropContainer
	{
	public:
		explicit PatchManager(juce::Component* _root);
		~PatchManager() override;

		void timerCallback() override;

		void setSelectedSearch(const pluginLib::patchDB::SearchHandle& _handle);

	private:
		Tree* m_tree = nullptr;
		List* m_list = nullptr;
	};
}