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
			// shown when it is the value, left out of the menu and the wheel steps
			bool hidden = false;
		};

		explicit ElemComboBox(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);
		~ElemComboBox() override;

		void setEntries(const std::vector<Entry>& _options);
		void setOptions(const std::vector<Rml::String>& _options);

		void addOption(const Rml::String& _option);

		void clearOptions();

		// Collapse the dropdown menu to one item per distinct label (opt-in, default off).
		// The value mapping used for display + selection still covers every entry, so the
		// box reflects any value; only the menu drops repeats.
		void setCollapseDuplicates(bool _collapse) { m_collapseDuplicates = _collapse; }

		void onChangeValue() override;

		void ProcessEvent(Rml::Event& _event) override;
		// The attribute group-size="N" opens a long list as submenus of N entries each, one
		// column apiece, named after the first word of their first and last entry ("1 - 32").
		void onClick(const Rml::Event& _event);
		// The list in its own window instead of inside the document, so it can extend past the
		// window and scroll rather than wrap into columns. Selected by the RML attribute
		// popup="window"; the default keeps the skinnable in-document menu.
		void openPopupWindow();
		void onMouseScroll(Rml::Event& _event);
		// The attribute drag-step="N" lets a drag step through the entries like a knob, one
		// entry per N dp, right or up for the next one. A press without a drag still opens
		// the menu.
		void onMouseDown(const Rml::Event& _event);
		void onDrag(const Rml::Event& _event);

		void OnAttributeChange(const Rml::ElementAttributes& _changedAttributes) override;

		void setSelectedIndex(size_t _index, bool _sendChangeEvent = true);
		int getSelectedIndex() const;

		void OnUpdate() override;

	private:
		int getIndexFromValue(int _value) const;
		// the entry _steps shown entries away from _index, hidden ones skipped, clamped to the ends
		int stepIndex(int _index, int _steps) const;

		bool updateValueText();

		std::vector<Entry> m_options;
		baseLib::EventListener<Rml::String> m_onOptionSelected;

		std::shared_ptr<Menu> m_menu;
		Rml::Element* m_textElem = nullptr;

		bool m_valueTextDirty;
		bool m_collapseDuplicates = false;

		float m_wheelAccum = 0.0f;	// wheel delta short of a notch

		float m_dragStep = 0.0f;	// dp per entry, 0 = no drag
		Rml::Vector2f m_dragStart;
		int m_dragStartIndex = -1;
		bool m_dragged = false;
	};
}
