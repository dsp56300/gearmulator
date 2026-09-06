#include "mqPartButton.h"
#include "mqEditor.h"
#include "mqPartSelect.h"
#include "mqPatchManager.h"

#include "jucePluginEditorLib/pluginProcessor.h"

#include "juceUiLib/messageBox.h"

namespace mqJucePlugin
{
	mqPartButton::mqPartButton(Rml::Element* _button, Editor& _editor)
	: PartButton(_button, _editor)
	, m_mqEditor(_editor)
	{
	}

	void mqPartButton::onClick(Rml::Event& _event)
	{
		m_mqEditor.getPartSelect()->selectPart(getPart());
	}

	void mqPartButton::dropFiles(const Rml::Event& _event, const juceRmlUi::FileDragData* _data, const std::vector<std::string>& _files)
	{
		auto* pm = dynamic_cast<PatchManager*>(m_mqEditor.getPatchManager());

		if (pm && !_files.empty())
		{
			// Peek at the first patch: Multi and Arrangement patches replace the
			// full device state, so loading one via a part-specific target does
			// not do what the user likely expects. Warn before proceeding.
			const auto patches = pm->loadPatchesFromFiles(std::vector<std::string>{_files.front()});

			if (!patches.empty())
			{
				const auto type = pm->detectPatchType(patches.front()->sysex);

				if (type == PatchManager::PatchType::Multi || type == PatchManager::PatchType::Arrangement)
				{
					const auto& name = m_mqEditor.getProcessor().getProperties().name;
					const std::string message = type == PatchManager::PatchType::Arrangement
						? "The dropped file contains an Arrangement patch. Loading it will "
						  "replace the full device state: the Multi setup and all Single "
						  "patches in the parts will be overwritten.\n\nDo you want to load it?"
						: "The dropped file contains a Multi patch. Loading it will replace "
						  "the Multi setup (part volumes, MIDI channels, FX, etc.) but the "
						  "Single patches currently loaded in the parts will be kept."
						  "\n\nDo you want to load it?";

					genericUI::MessageBox::showYesNo(genericUI::MessageBox::Icon::Question, name, message,
						[pm, patches](const genericUI::MessageBox::Result _result)
					{
						if (_result == genericUI::MessageBox::Result::Yes)
							pm->activatePatch(patches.front(), 0);
					});
					return;
				}
			}
		}

		jucePluginEditorLib::PartButton::dropFiles(_event, _data, _files);
	}
}
