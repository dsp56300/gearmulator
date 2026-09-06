#pragma once

#include <cstdint>
#include <string>

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class FocusedParameterTooltip
	{
	public:
		FocusedParameterTooltip(Rml::Element* _label);

		bool isValid() const { return m_label != nullptr; }
		uint32_t getTooltipDisplayTime() const;
		void setVisible(bool _visible) const;
		void initialize(const Rml::Element* _component, const std::string& _value) const;

	private:
		Rml::Element* m_label = nullptr;
	};
}
