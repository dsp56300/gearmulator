#include "listitem.h"

#include "patchmanagerUiRml.h"

#include "jucePluginLib/patchdb/patch.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

namespace jucePluginEditorLib::patchManagerRml
{
	ListItem::ListItem(PatchManagerUiRml& _pm, juceRmlUi::List& _list) : ListEntry(_list), m_patchManager(_pm)
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
			elem->onPatchChanged(_patch);

		return true;
	}

	ListElemEntry::ListElemEntry(const Rml::String& _tag) : ElemListEntry(_tag)
	{
		juceRmlUi::EventListener::Add(this, Rml::EventId::Mousedown, [this](Rml::Event& _event) { onMouseDown(_event); });
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
		{
			setName(_patch->getName());
			setColor(getItem()->getPatchManager().getPatchColor(_patch));
		}
	}

	void ListElemEntry::OnChildAdd(Rml::Element* _child)
	{
		ElemListEntry::OnChildAdd(_child);

		if (m_elemName == nullptr)
		{
			if (_child->GetId() == "name")
				m_elemName = _child;
		}
		if (m_elemColor == nullptr)
		{
			if (_child->GetId() == "color")
				m_elemColor = _child;
		}
	}

	void ListElemEntry::setName(const std::string& _name, bool _forceUpdate)
	{
		if (!_forceUpdate && _name == m_name)
			return;

		m_name = _name;

		if (m_elemName)
			m_elemName->SetInnerRML(Rml::StringUtilities::EncodeRml(_name));
	}

	void ListElemEntry::setColor(uint32_t _color, bool _forceUpdate)
	{
		if (!_forceUpdate && _color == m_color)
			return;

		m_color = _color;

		if (!m_elemColor)
			return;

		if (m_color == pluginLib::patchDB::g_invalidColor)
		{
			m_elemColor->SetProperty(Rml::PropertyId::Visibility, Rml::Property(Rml::Style::Visibility::Hidden));
			return;
		}

		const Rml::Colourb c = PatchManagerUiRml::toRmlColor(m_color);

		m_elemColor->SetProperty(Rml::PropertyId::Color     , Rml::Property(c, Rml::Unit::COLOUR));
		m_elemColor->SetProperty(Rml::PropertyId::ImageColor, Rml::Property(c, Rml::Unit::COLOUR));

		m_elemColor->SetProperty(Rml::PropertyId::Visibility, Rml::Property(Rml::Style::Visibility::Visible));
	}

	void ListElemEntry::onMouseDown(const Rml::Event& _event)
	{
		const auto button = juceRmlUi::helper::getMouseButton(_event);
		if (button == 1)
			onRightClick(_event);
	}

	void ListElemEntry::onRightClick(const Rml::Event& _event)
	{

	}
}
