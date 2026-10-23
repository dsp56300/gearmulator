#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "baseLib/event.h"

namespace juceRmlUi
{
	class ElemComboBox;
}

namespace jucePluginEditorLib
{
	class Editor;

	// A combo box with the firmware images of the processor: choosing one selects it, the entry follows a selection
	// made elsewhere, by a state the host loads for example, and goes back if the device of an image cannot be created
	class FirmwareSelector
	{
	public:
		FirmwareSelector(Editor& _editor, juceRmlUi::ElemComboBox* _comboBox, const std::string& _emptyText = "<No ROM found>");

	private:
		void update() const;

		Editor& m_editor;
		juceRmlUi::ElemComboBox* const m_comboBox;
		const std::string m_emptyText;
		const std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
		baseLib::EventListener<uint32_t> m_onFirmwareChanged;
	};
}
