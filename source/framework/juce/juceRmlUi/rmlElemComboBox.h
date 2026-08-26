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
		struct Entry
		{
			std::string text;
			int value;
		};

		explicit ElemComboBox(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~ElemComboBox() override;

		void setEntries(const std::vector<Entry>& _options);
		void setOptions(const std::vector<Rml::String>& _options);

		void addOption(const Rml::String& _option);

		void clearOptions();

		void onChangeValue() override;

		void ProcessEvent(Rml::Event& _event) override;
		void onClick(const Rml::Event& _event);
		void onMouseScroll(const Rml::Event& _event);

		void setSelectedIndex(size_t _index, bool _sendChangeEvent = true);
		int getSelectedIndex() const;

		void OnUpdate() override;

	private:
		int getIndexFromValue(int _value) const;

		bool updateValueText();

		std::vector<Entry> m_options;
		baseLib::EventListener<Rml::String> m_onOptionSelected;

		std::shared_ptr<Menu> m_menu;
		Rml::Element* m_textElem = nullptr;

		bool m_valueTextDirty;
	};
}
