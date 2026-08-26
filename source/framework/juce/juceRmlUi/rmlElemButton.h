#pragma once

#include "rmlElemValue.h"

namespace juceRmlUi
{
	class ElemButton : public ElemValue
	{
	public:
		baseLib::Event<ElemButton*> evClick;

		explicit ElemButton(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);

		void onChangeValue() override;
		void onPropertyChanged(const std::string& _key) override;

		bool getChecked() const { return m_isChecked; }

		void OnChildAdd(Rml::Element* _child) override;
		void OnChildRemove(Rml::Element* _child) override;
		bool IsPointWithinElement(Rml::Vector2f _point) override;

		void setChecked(bool _checked);

		static void setChecked(Rml::Element* _button, bool _checked);
		static bool isChecked(const Rml::Element* _button);
		static bool isPressed(const Rml::Element* _element);

		bool isChecked() const { return m_isChecked; }
		bool isPressed() const;
		bool isToggle() const { return m_isToggle; }

		static bool isToggle(const Rml::Element* _button);

		int getValueOn() const;
		int getValueOff() const;

	private:
		void onClick();

		bool m_isToggle = false;
		bool m_isChecked = false;

		int m_valueOn = -1;
		int m_valueOff = -1;

		Rml::Element* m_hitTestElem = nullptr;
	};
}
