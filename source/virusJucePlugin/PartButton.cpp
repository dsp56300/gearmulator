#include "PartButton.h"

#include "VirusEditor.h"
#include "VirusController.h"

#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

#include "RmlUi/Core/Element.h"

namespace genericVirusUI
{
	PartButton::PartButton(Rml::Element* _button, VirusEditor& _editor) : jucePluginEditorLib::PartButton(_button, _editor), m_editor(_editor)
	{
	}

	bool PartButton::canDrop(const Rml::Event& _event, const juceRmlUi::DragSource* _source)
	{
		if(getPart() > 0 && !m_editor.getController().isMultiMode())
			return false;

		return jucePluginEditorLib::PartButton::canDrop(_event, _source);
	}

	void PartButton::onClick(Rml::Event& _e)
	{
		selectPreset(_e, getPart());
	}

	void PartButton::setButtonText(const std::string& _text)
	{
		getElement()->SetInnerRML(_text);
	}

	void PartButton::selectPreset(const Rml::Event& _event, uint8_t _part) const
	{
		auto eventMousePos = juceRmlUi::helper::getMousePos(_event);
		auto* eventTargetElem = _event.GetTargetElement();

		pluginLib::patchDB::SearchRequest req;
		req.sourceType = pluginLib::patchDB::SourceType::Rom;

		m_editor.getPatchManager()->search(std::move(req), [this, eventMousePos, eventTargetElem](const pluginLib::patchDB::Search& _search)
		{
			std::map<std::string, std::vector<pluginLib::patchDB::PatchPtr>> patches;

			{
				std::shared_lock lock(_search.resultsMutex);
				for (const auto& patch : _search.results)
				{
					const auto s = patch->source.lock();
					if(!s)
						continue;
					patches[s->name].push_back(patch);
				}
			}

			for (auto& it : patches)
			{
				std::sort(it.second.begin(), it.second.end(), [](const pluginLib::patchDB::PatchPtr& _a, const pluginLib::patchDB::PatchPtr& _b)
				{
					return _a->program < _b->program;
				});
			}

			juce::MessageManager::callAsync([this, patches = std::move(patches), eventMousePos, eventTargetElem]
			{
				juceRmlUi::Menu selector;

				for (const auto& it : patches)
				{
					juceRmlUi::Menu p;
					for (const auto& patch : it.second)
					{
		                const auto& presetName = patch->getName();
		                p.addEntry(presetName, [this, patch] 
		                {
							if(m_editor.getPatchManager()->activatePatch(patch, getPart()))
								m_editor.getPatchManager()->setSelectedPatch(getPart(), patch);
		                });
					}
		            selector.addSubMenu(it.first, std::move(p));
				}
				selector.runModal(eventTargetElem, eventMousePos, 16);
			});
		});
	}
}
