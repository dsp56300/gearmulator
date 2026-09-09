#pragma once

#include "rmlElemValue.h"
#include "rmlMenu.h"

#include "baseLib/event.h"

#include "RmlUi/Core/EventListener.h"

namespace juce
{
	class LookAndFeel;
}

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

		// Collapse the dropdown menu to one item per distinct label (opt-in, default off).
		// The value mapping used for display + selection still covers every entry, so the
		// box reflects any value; only the menu drops repeats. Meant for algorithm lists
		// that want one entry per base structure, where variants share a base name.

		void onChangeValue() override;

		void ProcessEvent(Rml::Event& _event) override;
		void onClick(const Rml::Event& _event);
		// The list in its own window instead of inside the document, so it can extend past the
		// window and scroll rather than wrap into columns. Selected by the RML attribute
		// popup="window"; the default keeps the skinnable in-document menu.
		void openPopupWindow();
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
		std::shared_ptr<juce::LookAndFeel> m_popupLookAndFeel;
		Rml::Element* m_textElem = nullptr;

		bool m_valueTextDirty;
	};
}
