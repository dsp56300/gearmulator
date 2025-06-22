#include "listitem.h"

#include "jucePluginLib/patchdb/patch.h"

namespace jucePluginEditorLib::patchManagerRml
{
	ListItem::ListItem(juceRmlUi::List& _list): ListEntry(_list)
	{
	}

	ListElemEntry* ListItem::getListElemEntry() const
	{
		return dynamic_cast<ListElemEntry*>(getElement());
	}

	bool ListItem::setPatch(const pluginLib::patchDB::PatchPtr& _patch)
	{
		if (_patch == m_patch)
			return false;
		m_patch = _patch;

		if (auto* elem = getListElemEntry())
		{
			elem->onPatchChanged(_patch);
		}
		return true;
	}

	ListElemEntry::ListElemEntry(const Rml::String& _tag): ElemListEntry(_tag)
	{
	}

	void ListElemEntry::onEntryChanged()
	{
//		ElemListEntry::onEntryChanged();
		m_item = dynamic_cast<ListItem*>(getEntry().get());

		if (!m_item)
			return;

		onPatchChanged(m_item->getPatch());
	}

	void ListElemEntry::onPatchChanged(const pluginLib::patchDB::PatchPtr& _patch)
	{
		if (_patch)
			SetInnerRML(Rml::StringUtilities::EncodeRml(_patch->getName()));
	}
}
