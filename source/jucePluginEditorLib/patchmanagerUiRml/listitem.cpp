#include "listitem.h"

#include "patchmanagerUiRml.h"
#include "jucePluginEditorLib/patchmanager/patchmanager.h"
#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

#include "jucePluginLib/patchdb/patch.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlMenu.h"

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

	ListElemEntry::ListElemEntry(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : ElemListEntry(_coreInstance, _tag)
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

	void ListElemEntry::onMouseDown(Rml::Event& _event)
	{
		if (juceRmlUi::helper::getMouseButton(_event) == juceRmlUi::MouseButton::Right)
		{
			if (!m_item->isSelected())
				getList().setSelectedEntries({m_item->getIndex()});
			onRightClick(_event);
			_event.StopPropagation();
		}
	}

	void ListElemEntry::onRightClick(const Rml::Event& _event)
	{
		auto& list = getList();

		list.openContextMenu(_event);
	}

	ListModel& ListElemEntry::getList() const
	{
		auto* item = getItem();
		assert(item);
		return item->getPatchManager().getListModel();
	}

	std::unique_ptr<juceRmlUi::DragData> ListElemEntry::createDragData()
	{
		const auto index = getItem()->getIndex();

		auto indices = getList().getSelectedEntries();
		if (indices.empty() || std::find(indices.begin(), indices.end(), index) == indices.end())
		{
			indices.push_back(index);
			getItem()->getList().setSelected(index, true, false, true);
		}

		std::map<uint32_t, pluginLib::patchDB::PatchPtr> patches;

		for (const auto i : indices)
		{
			const auto& patch = getList().getPatches()[i];
			patches.insert({ static_cast<uint32_t>(i), patch });
		}

		return std::make_unique<patchManager::SavePatchDesc>(getItem()->getPatchManager().getEditor(), std::move(patches));
	}

	bool ListElemEntry::canDrop(const Rml::Event& _event, const DragSource* _source)
	{
		if (getList().getSourceType() != pluginLib::patchDB::SourceType::LocalStorage)
			return ElemListEntry::canDrop(_event, _source);

		auto* elem = _source->getElement();
		auto* listElemSource = dynamic_cast<ListElemEntry*>(elem);

		if (listElemSource)
		{
			// the list itself is being sorted
			setAllowLocations(false, true);
			return true;
		}

		const auto& patches = patchManager::SavePatchDesc::getPatchesFromDragSource(*_source);
		if (patches.empty())
			return ElemListEntry::canDrop(_event, _source);

		// allow to replace only
		setAllowLocations(false, false);

		return true;
	}

	bool ListElemEntry::canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files)
	{
		auto& list = getList();
		if (list.getSourceType() != pluginLib::patchDB::SourceType::LocalStorage)
			return false;
		return true;
	}

	void ListElemEntry::drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data)
	{
		const auto* data = dynamic_cast<const patchManager::SavePatchDesc*>(_data);
		if (!data)
			return ElemListEntry::drop(_event, _source, _data);
		const auto& patches = patchManager::SavePatchDesc::getPatchesFromDragData(_data);
		if (patches.empty())
			return ElemListEntry::drop(_event, _source, _data);

		auto& db = getList().getPatchManager().getDB();

		auto index = getItem()->getIndex();

		if (getDragLocationV() == DragLocation::Bottom)
			++index;

		if(dynamic_cast<ListElemEntry*>(juceRmlUi::helper::getDragElement(_event)))
		{
			// if the source is a list item entry too, we move the patches around
			db.movePatchesTo(static_cast<uint32_t>(index), patches);
			getList().setContent(getList().getSearchHandle());
		}

		if (patches.size() == 1 && data->isPartValid())
		{
			if(getDragLocationV() == DragLocation::Center)
			{
				const auto existingPatch = getList().getPatch(index);

				if(existingPatch)
				{
					genericUI::MessageBox::showYesNo(juce::MessageBoxIconType::QuestionIcon,
						"Replace Patch", 
						"Do you want to replace the existing patch '" + existingPatch->name + "' with contents of part " + std::to_string(data->getPart() + 1) + "?", 
						[this, existingPatch, patches](const genericUI::MessageBox::Result _result)
						{
							if (_result == genericUI::MessageBox::Result::Yes)
							{
								getList().getPatchManager().getDB().replacePatch(existingPatch, patches.front());
							}
						});
					return;
				}
			}

#if SYNTHLIB_DEMO_MODE
			pm.getEditor().showDemoRestrictionMessageBox();
#else

			const auto& source = getList().getDatasource();
			if(!source)
				return;

			const auto part = data->getPart();

			getList().getDB().copyPatchesTo(source, patches, static_cast<int>(index), [this, part](const std::vector<pluginLib::patchDB::PatchPtr>& _patches)
			{
				juce::MessageManager::callAsync([this, part, _patches]
				{
					getList().getDB().setSelectedPatch(part, _patches.front());
				});
			});
#endif
		}
	}

	void ListElemEntry::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		auto& list = getList();

		if (list.getSourceType() != pluginLib::patchDB::SourceType::LocalStorage)
			return;

		const auto patches = getList().getDB().loadPatchesFromFiles(_files);

		if (patches.empty())
			return;

		const auto ds = getList().getDatasource();

		if (!ds)
			return;

		auto index = getItem()->getIndex();

		if (getDragLocationV() == DragLocation::Bottom)
			++index;

		getList().getDB().copyPatchesTo(ds, patches, static_cast<int>(index));
	}
}
