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

	private:
		void onClick();
		void onMouseDown();
		void onMouseUp();

		void setChecked(bool _checked);

		bool m_isToggle = false;
		bool m_isChecked = false;

		pluginLib::ParamValue m_valueOn = -1;
		pluginLib::ParamValue m_valueOff = -1;
	};
}
