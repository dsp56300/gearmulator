#include "PartButton.h"

#include "PatchManager.h"
#include "VirusEditor.h"
#include "VirusProcessor.h"

#include "jucePluginEditorLib/patchmanager/savepatchdesc.h"

#include "RmlUi/Core/Element.h"

namespace genericVirusUI
{
	PartButton::PartButton(Rml::Element* _button, VirusEditor& _editor) : jucePluginEditorLib::PartButton(_button, _editor), m_editor(_editor)
	{
		std::lock_guard lock(getInstancesMutex());
		getInstances().insert(this);
	}

	PartButton::~PartButton()
	{
		std::lock_guard lock(getInstancesMutex());
		getInstances().erase(this);
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

		pluginLib::patchDB::SearchRequest req;
		req.sourceType = pluginLib::patchDB::SourceType::Rom;

		// called on the loader thread, this button and its element may be gone by the time the message thread runs the menu
		m_editor.getPatchManager()->search(std::move(req), [this, eventMousePos](const pluginLib::patchDB::Search& _search)
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

			juce::MessageManager::callAsync([this, patches = std::move(patches), eventMousePos]
			{
				if(!isAlive(this))
					return;

				const auto currentPatch = m_editor.getPatchManager()->getState().getPatch(getPart());

				juceRmlUi::Menu selector;

				for (const auto& it : patches)
				{
					juceRmlUi::Menu p;
					for (const auto& patch : it.second)
					{
		                const auto& presetName = patch->getName();
						const bool checked = pluginLib::patchDB::PatchKey(*patch) == currentPatch;
		                p.addEntry(presetName, true, checked, [this, patch]
		                {
							if(!isAlive(this))
								return;
							if(m_editor.getPatchManager()->activatePatch(patch, getPart()))
								m_editor.getPatchManager()->setSelectedPatch(getPart(), patch);
		                });
					}
		            selector.addSubMenu(it.first, std::move(p));
				}
				// the button, not the target of the click: setButtonText() replaces the elements inside of it
				selector.runModal(getElement(), eventMousePos, 16);
			});
		});
	}

	std::mutex& PartButton::getInstancesMutex()
	{
		static std::mutex mutex;
		return mutex;
	}

	std::set<const PartButton*>& PartButton::getInstances()
	{
		static std::set<const PartButton*> instances;
		return instances;
	}

	bool PartButton::isAlive(const PartButton* _button)
	{
		std::lock_guard lock(getInstancesMutex());
		return getInstances().find(_button) != getInstances().end();
	}
}
