#pragma once

#include "jucePluginLib/patchdb/patchdbtypes.h"
#include "juceRmlUi/rmlElemListEntry.h"
#include "juceRmlUi/rmlListEntry.h"

namespace jucePluginEditorLib::patchManagerRml
{
	class ListModel;
}

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
		explicit ListElemEntry(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);

		void onEntryChanged() override;

		ListItem* getItem() const { return dynamic_cast<ListItem*>(getEntry().get()); }

		void onPatchChanged(const pluginLib::patchDB::PatchPtr& _patch);

		void OnChildAdd(Rml::Element* _child) override;

		void setName(const std::string& _name, bool _forceUpdate = false);
		void setColor(uint32_t _color, bool _forceUpdate = false);

		void onMouseDown(Rml::Event& _event);

		ListModel& getList() const;

		std::unique_ptr<juceRmlUi::DragData> createDragData() override;

		bool canDrop(const Rml::Event& _event, const DragSource* _source) override;
		bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) override;
		void drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data) override;
		void dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files) override;

	private:
		Rml::Element* m_elemName = nullptr;
		Rml::Element* m_elemColor = nullptr;

		std::string m_name;
		uint32_t m_color = 0xffffffff;
	};
}
