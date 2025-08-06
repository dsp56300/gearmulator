#pragma once

#include "rmlElemValue.h"
#include "jucePluginLib/types.h"

namespace juceRmlUi
{
	class ElemButton : public ElemValue
	{
	public:
		explicit ElemButton(const Rml::String& _tag);

		void onChangeValue() override;
		void onPropertyChanged(const std::string& _key) override;

		bool getChecked() const { return m_isChecked; }

		void OnChildAdd(Rml::Element* child) override;
		void OnChildRemove(Rml::Element* _child) override;
		bool IsPointWithinElement(Rml::Vector2f _point) override;

		void setChecked(bool _checked);

	private:
		void onClick();
		void onMouseDown();
		void onMouseUp();

		bool m_isToggle = false;
		bool m_isChecked = false;

		pluginLib::ParamValue m_valueOn = -1;
		pluginLib::ParamValue m_valueOff = -1;

		Rml::Element* m_hitTestElem = nullptr;
	};
}
