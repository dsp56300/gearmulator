#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "../../jucePluginLib/patchdb/db.h"

namespace jucePluginEditorLib
{
	class PatchManagerTree;

	class PatchManager : public pluginLib::patchDB::DB, juce::Timer
	{
	public:
		explicit PatchManager(juce::Component *_root);
		~PatchManager() override;

		void addCategories(const std::vector<std::string>& _categories)
		{
			for (const auto& category : _categories)
				addCategory(category);
		}

		void addCategory(const std::string& _category);

		void timerCallback() override;
	private:
		PatchManagerTree* m_tree = nullptr;
	};
}