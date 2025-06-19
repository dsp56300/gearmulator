#pragma once

#include "jucePluginLib/patchdb/patchdbtypes.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace pluginLib::patchDB
{
	class DB;
	class Tags;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManagerUiJuce;

	class Info final : public juce::Viewport
	{
	public:
		Info(PatchManagerUiJuce& _pm);
		~Info() override;

		void setPatch(const pluginLib::patchDB::PatchPtr& _patch);
		void clear();

		static std::string toText(const pluginLib::patchDB::Tags& _tags);
		static std::string toText(const pluginLib::patchDB::DataSourceNodePtr& _source);

		void paint(juce::Graphics& g) override;

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

	private:
		juce::Label* addChild(juce::Label* _label);
		void doLayout() const;
		void resized() override;

		pluginLib::patchDB::DB& m_patchManager;

		Component m_content;

		juce::Label* m_name = nullptr;
		juce::Label* m_lbSource = nullptr;
		juce::Label* m_source = nullptr;
		juce::Label* m_lbCategories = nullptr;
		juce::Label* m_categories = nullptr;
		juce::Label* m_lbTags = nullptr;
		juce::Label* m_tags = nullptr;

		pluginLib::patchDB::PatchPtr m_patch;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	};
}
