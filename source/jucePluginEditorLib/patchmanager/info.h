#pragma once

#include <juce_audio_plugin_client/juce_audio_plugin_client.h>

#include "../../jucePluginLib/patchdb/patchdbtypes.h"

namespace pluginLib::patchDB
{
	class Tags;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class Info final : public juce::Viewport
	{
	public:
		Info(PatchManager& _pm);
		~Info() override;

		void setPatch(const pluginLib::patchDB::PatchPtr& _patch) const;
		void clear() const;

		static std::string toText(const pluginLib::patchDB::Tags& _tags);
		static std::string toText(const pluginLib::patchDB::DataSourcePtr& _source);
	private:
		juce::Label* addChild(juce::Label* _label);
		void doLayout() const;
		void resized() override;

		PatchManager& m_patchManager;

		juce::Component m_content;

		juce::Label* m_name = nullptr;
		juce::Label* m_lbSource = nullptr;
		juce::Label* m_source = nullptr;
		juce::Label* m_lbCategories = nullptr;
		juce::Label* m_categories = nullptr;
		juce::Label* m_lbTags = nullptr;
		juce::Label* m_tags = nullptr;
	};
}
