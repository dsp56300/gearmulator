#pragma once
#include "types.h"
#include "jucePluginLib/patchdb/datasource.h"

namespace pluginLib::patchDB
{
	struct Dirty;
}

namespace jucePluginEditorLib
{
	class Editor;
}

namespace jucePluginEditorLib::patchManager
{
	class PatchManager;

	class PatchManagerUi
	{
	public:
		PatchManagerUi(Editor& _editor, PatchManager& _db);
		virtual ~PatchManagerUi() = default;

		Editor& getEditor() const { return m_editor; }

		auto& getDB() const { return m_db; }

		bool isScanning() const;

		virtual void processDirty(const pluginLib::patchDB::Dirty& _dirty) = 0;

		virtual bool setSelectedDataSource(const pluginLib::patchDB::DataSourceNodePtr& _ds) = 0;

		virtual void setSelectedPatch(const pluginLib::patchDB::PatchPtr& _patch) = 0;
		virtual void setSelectedPatches(const std::set<pluginLib::patchDB::PatchPtr>& _patches) = 0;
		virtual	bool setSelectedPatches(const std::set<pluginLib::patchDB::PatchKey>& _patches) = 0;

		virtual void setCustomSearch(pluginLib::patchDB::SearchHandle _sh) = 0;
		virtual void bringToFront() = 0;

		virtual pluginLib::patchDB::SearchHandle getSearchHandle(const pluginLib::patchDB::DataSource& _ds, bool _selectTreeItem) = 0;
		virtual bool createTag(GroupType _group, const std::string& _name) = 0;

		static void sortPatches(std::vector<pluginLib::patchDB::PatchPtr>& _patches, pluginLib::patchDB::SourceType _sourceType);

	private:
		Editor& m_editor;
		PatchManager& m_db;
	};
}
