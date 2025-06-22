#pragma once

#include "jucePluginLib/patchdb/patchdbtypes.h"
#include "juceRmlUi/rmlElemListEntry.h"
#include "juceRmlUi/rmlListEntry.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class ListElemEntry;

	class ListItem final : public juceRmlUi::ListEntry
	{
	public:
		explicit ListItem(juceRmlUi::List& _list);

		ListElemEntry* getListElemEntry() const;

		bool setPatch(const pluginLib::patchDB::PatchPtr& _patch);
		const auto& getPatch() const { return m_patch; }

	private:
		pluginLib::patchDB::PatchPtr m_patch;
	};

	class ListElemEntry final : public juceRmlUi::ElemListEntry
	{
	public:
		explicit ListElemEntry(const Rml::String& _tag);

		void onEntryChanged() override;

		ListItem* getItem() const { return m_item; }

		void onPatchChanged(const pluginLib::patchDB::PatchPtr& _patch);

	private:
		ListItem* m_item = nullptr;
	};
}
