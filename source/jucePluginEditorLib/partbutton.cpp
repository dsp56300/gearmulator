#include "partbutton.h"

#include "pluginEditor.h"
#include "pluginProcessor.h"
#include "juceRmlUi/rmlElemList.h"

#include "juceRmlUi/rmlEventListener.h"
#include "juceRmlUi/rmlHelper.h"

#include "patchmanager/patchmanager.h"
#include "patchmanager/savepatchdesc.h"

#include "patchmanagerUiRml/listmodel.h"

namespace jucePluginEditorLib
{
	namespace
	{
		std::pair<pluginLib::patchDB::PatchPtr, patchManagerRml::ListModel*> getPatchFromDragSource(const Rml::Event& _event, const juceRmlUi::DragSource* _source)
		{
			auto* listElem = dynamic_cast<juceRmlUi::ElemList*>(juceRmlUi::helper::getDragElement(_event));
			if(!listElem)
				return {};

			patchManagerRml::ListModel* listModel = static_cast<patchManagerRml::ListModel*>(listElem->GetAttribute<void*>("model", nullptr));

			const auto& patches = patchManager::SavePatchDesc::getPatchesFromDragSource(*_source);

			if (patches.size() != 1)
				return {};

			return {patches.front(), listModel};
		}
	}

	PartButton::PartButton(Rml::Element* _button, Editor& _editor): m_button(_button), m_editor(_editor)
	{
		juceRmlUi::EventListener::Add(_button, Rml::EventId::Click, [this](Rml::Event& _event)
		{
			onClick();
		});

		DragSource::init(_button);
		DragTarget::init(_button);
	}

	void PartButton::initalize(const uint8_t _part)
	{
		m_part = _part;

		m_button->SetProperty(Rml::PropertyId::Drag, Rml::Style::Drag::Clone);
	}

	bool PartButton::canDrop(const Rml::Event& _event, const DragSource* _source)
	{
		const auto* savePatchDesc = patchManager::SavePatchDesc::fromDragSource(*_source);

		if(savePatchDesc)
			return !savePatchDesc->isPartValid() || savePatchDesc->getPart() != getPart();

		const auto patch = getPatchFromDragSource(_event, _source);
		return patch.first != nullptr;
	}

	bool PartButton::canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files)
	{
		return _files.size() == 1;
	}

	void PartButton::drop(const Rml::Event& _event, const DragSource* _source, const juceRmlUi::DragData* _data)
	{
		const auto* savePatchDesc = dynamic_cast<const patchManager::SavePatchDesc*>(_data);

		auto* pm = m_editor.getPatchManager();

		if(savePatchDesc)
		{
			// part is not valid if the drag source is the patch manager
			if(savePatchDesc->isPartValid())
			{
				if(savePatchDesc->getPart() == getPart())
					return;
				pm->copyPart(m_part, static_cast<uint8_t>(savePatchDesc->getPart()));
			}
		}

		const auto [patch, list] = getPatchFromDragSource(_event, _source);
		if(!patch)
			return DragTarget::drop(_event, _source, _data);

		if(pm->getCurrentPart() == m_part)
		{
			list->setSelectedPatches({patch});
			list->activateSelectedPatch();
		}
		else
		{
			pm->setSelectedPatch(m_part, patch, list->getSearchHandle());
		}
	}

	void PartButton::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		auto* pm = m_editor.getPatchManager();

		if(!pm)
			return DragTarget::dropFiles(_event, _data, _files);

		if(!pm->activatePatch(_files.front(), getPart()))
		{
			genericUI::MessageBox::showOk(juce::MessageBoxIconType::WarningIcon, 
				m_editor.getProcessor().getProperties().name, 
				"Failed to load patch. Make sure that the format is supported and that the file only contains one patch.\n"
				"\n"
				"Drag files with multiple patches onto the Patch Manager instead to import them");
		}

		DragTarget::dropFiles(_event, _data, _files);
	}

	std::unique_ptr<juceRmlUi::DragData> PartButton::createDragData()
	{
		return std::make_unique<patchManager::SavePatchDesc>(m_editor, m_part);
	}

	void PartButton::setVisible(const bool _visible)
	{
		juceRmlUi::helper::setVisible(m_button, _visible);
	}
}
