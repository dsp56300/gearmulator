#pragma once

#include "rmlElemValue.h"
#include "rmlMenu.h"

#include "baseLib/event.h"

#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class ElemComboBox : public ElemValue, Rml::EventListener
	{
	public:
		explicit ElemComboBox(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~ElemComboBox() override;

		void setOptions(const std::vector<Rml::String>& _options);

		void addOption(const Rml::String& _option);

		void clearOptions();

		void onChangeValue() override;

		void ProcessEvent(Rml::Event& _event) override;
		void onClick(const Rml::Event& _event);

		void setSelectedIndex(size_t _index, bool _sendChangeEvent = true);
		int getSelectedIndex() const;

	private:
		void updateValueText();

		std::vector<Rml::String> m_options;
		baseLib::EventListener<Rml::String> m_onOptionSelected;

		std::shared_ptr<Menu> m_menu;
		Rml::Element* m_textElem = nullptr;
	};
}
