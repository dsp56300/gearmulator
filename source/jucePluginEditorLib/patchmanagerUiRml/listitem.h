#pragma once

#include "jucePluginLib/patchdb/patchdbtypes.h"
#include "juceRmlUi/rmlElemListEntry.h"
#include "juceRmlUi/rmlListEntry.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class PatchManagerUiRml;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class ListElemEntry;

	class ListItem final : public juceRmlUi::ListEntry
	{
	public:
		explicit ListItem(PatchManagerUiRml& _pm, juceRmlUi::List& _list);

		ListElemEntry* getListElemEntry() const;

		bool setPatch(const pluginLib::patchDB::PatchPtr& _patch);
		const auto& getPatch() const { return m_patch; }

		auto& getPatchManager() const { return m_patchManager; }

	private:
		pluginLib::patchDB::PatchPtr m_patch;
		PatchManagerUiRml& m_patchManager;
	};

	class ListElemEntry final : public juceRmlUi::ElemListEntry
	{
	public:
		explicit ListElemEntry(const Rml::String& _tag);

		void onEntryChanged() override;

		ListItem* getItem() const { return m_item; }

		void onPatchChanged(const pluginLib::patchDB::PatchPtr& _patch);

		void OnChildAdd(Rml::Element* _child) override;

		void setName(const std::string& _name, bool _forceUpdate = false);
		void setColor(uint32_t _color, bool _forceUpdate = false);

		void onMouseDown(const Rml::Event& _event);
		void onRightClick(const Rml::Event& _event);

	private:
		ListItem* m_item = nullptr;
		Rml::Element* m_elemName = nullptr;
		Rml::Element* m_elemColor = nullptr;

		std::string m_name;
		uint32_t m_color = 0xffffffff;
	};
}
