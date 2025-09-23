#pragma once

#include "rmlElemValue.h"
#include "jucePluginLib/types.h"

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

	private:
		void onClick();
		void onMouseDown();
		void onMouseUp();

		pluginLib::ParamValue getValueOn() const;
		pluginLib::ParamValue getValueOff() const;

		bool m_isToggle = false;
		bool m_isChecked = false;

		pluginLib::ParamValue m_valueOn = -1;
		pluginLib::ParamValue m_valueOff = -1;

		Rml::Element* m_hitTestElem = nullptr;
	};
}
