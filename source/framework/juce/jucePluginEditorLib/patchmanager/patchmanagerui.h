#pragma once

#include "types.h"

#include "jucePluginLib/patchdb/datasource.h"
#include "jucePluginLib/patchmanager/uiInterface.h"

namespace jucePluginEditorLib
{
	class Editor;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class PatchManagerUi : public pluginLib::patchManager::UiInterface
	{
	public:
		PatchManagerUi(Editor& _editor, PatchManager& _db);

		Editor& getEditor() const { return m_editor; }

		auto& getDB() const { return m_db; }

		bool isScanning() const;

		virtual bool setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds) = 0;

		virtual bool createTag(GroupType _group, const std::string& _name) = 0;

	private:
		Editor& m_editor;
		PatchManager& m_db;
	};
}
