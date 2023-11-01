#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "../../jucePluginLib/patchdb/db.h"

namespace jucePluginEditorLib::patchManager
{
	class Tree;

	class PatchManager : public pluginLib::patchDB::DB, juce::Timer
	{
	public:
		explicit PatchManager(juce::Component *_root);
		~PatchManager() override;

		void timerCallback() override;

	private:
		Tree* m_tree = nullptr;
	};
}