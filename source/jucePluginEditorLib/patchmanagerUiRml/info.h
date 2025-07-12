#pragma once

#include "jucePluginLib/patchdb/patchdbtypes.h"

#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerDataModel;
}

namespace pluginLib::patchDB
{
	class DB;
	class Tags;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml;

	class Info
	{
	public:
		Info(const PatchManagerUiRml& _pm);

		void setPatch(const pluginLib::patchDB::PatchPtr& _patch);
		void clear();

		static std::string toText(const pluginLib::patchDB::Tags& _tags);
		static std::string toText(const pluginLib::patchDB::DataSourceNodePtr& _source);

		void processDirty(const pluginLib::patchDB::Dirty& _dirty);

	private:
		pluginLib::patchDB::DB& m_patchManager;
		PatchManagerDataModel& m_dataModel;

		pluginLib::patchDB::PatchPtr m_patch;
		pluginLib::patchDB::SearchHandle m_searchHandle = pluginLib::patchDB::g_invalidSearchHandle;
	};
}
