#pragma once

#include <set>

#include "jucePluginLib/patchdb/patch.h"

namespace pluginLib::patchDB
{
	struct Dirty;
	struct DataSource;
}

namespace pluginLib::patchManager
{
	// What the patch manager tells the user interface that shows it. The patch manager works without one.
	class UiInterface
	{
	public:
		virtual ~UiInterface() = default;

		virtual void processDirty(const patchDB::Dirty& _dirty) = 0;

		virtual void setSelectedPatch(const patchDB::PatchPtr& _patch) = 0;
		virtual void setSelectedPatches(const std::set<patchDB::PatchPtr>& _patches) = 0;
		virtual bool setSelectedPatches(const std::set<patchDB::PatchKey>& _patches) = 0;

		virtual void setCustomSearch(patchDB::SearchHandle _sh) = 0;
		virtual void bringToFront() = 0;

		// the search that lists the patches of _ds, selected in the user interface if _selectTreeItem is set
		virtual patchDB::SearchHandle getSearchHandle(const patchDB::DataSource& _ds, bool _selectTreeItem) = 0;
	};
}
