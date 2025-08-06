#include "rmlParameterBinding.h"

#include "rmlParameterRef.h"

#include "jucePluginLib/parameterdescriptions.h"

#include "juceRmlUi/rmlDataProvider.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"

namespace rmlPlugin
{
	RmlParameterBinding::RmlParameterBinding(pluginLib::Controller& _controller, Rml::Context* _context)
	: m_controller(_controller)
	, m_component(juceRmlUi::RmlInterfaces::getCurrentComponent())
	{
		m_onCurrentPartChanged.set(_controller.onCurrentPartChanged, [this](const uint8_t _part)
		{
			setCurrentPart(_part);
		});

		bindParameters(_context, 16);
	}

	void RmlParameterBinding::bindParameters(Rml::Context* _context, const uint8_t _partCount)
	{
		for (uint8_t part = 0; part < _partCount; ++part)
			bindParametersForPart(_context, part, part);

		bindParametersForPart(_context, CurrentPart, m_controller.getCurrentPart());
	}

	void RmlParameterBinding::bind(Rml::Element& _element, const std::string& _parameterName, const uint8_t _part) const
	{
		bind(m_controller, _element, _parameterName, _part);
	}

	void RmlParameterBinding::bind(const pluginLib::Controller& _controller, Rml::Element& _element, const std::string& _parameterName, uint8_t _part)
	{
		const auto param = RmlParameterRef::createVariableName(_parameterName);

		juceRmlUi::helper::changeAttribute(&_element, "param", _parameterName);
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-min", param + "_min");
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-max", param + "_max");
		juceRmlUi::helper::changeAttribute(&_element, "data-value", param + "_value");

		if (auto* combo = dynamic_cast<juceRmlUi::ElemComboBox*>(&_element))
		{
			std::vector<Rml::String> options;
			auto* p = _controller.getParameter(_parameterName, _part);

			if (!p)
			{
				std::stringstream ss;
				ss << "Failed to find parameter " << _parameterName << " for combo box";
				Rml::Log::Message(Rml::Log::LT_ERROR, ss.str().c_str());
				return;
			}

			combo->setOptions(p->getDescription().valueList.texts);
		}
	}

	void RmlParameterBinding::setCurrentPart(const uint8_t _part)
	{
		juceRmlUi::RmlInterfaces::ScopedAccess access(m_component);
		for (auto& param : m_parametersPerPart[CurrentPart])
			param.changePart(m_controller, _part);
	}

	void RmlParameterBinding::bindParametersForPart(Rml::Context* _context, const uint8_t _targetPart, const uint8_t _sourcePart)
	{
		const auto& descs = m_controller.getParameterDescriptions().getDescriptions();

		const std::string partName = (_targetPart == CurrentPart ? "Current" : std::to_string(_sourcePart));

		auto model = _context->CreateDataModel("part" + partName);

		m_parametersPerPart[_targetPart].reserve(descs.size());

		for (uint32_t i=0; i<static_cast<uint32_t>(descs.size()); ++i)
		{
			auto* param = m_controller.getParameter(i, _sourcePart);

			if (param->getDescription().isNonPartSensitive())
			{
				// non-part sensitive parameters are always bound to the current part and to part 0 but not any other part
				if (_targetPart != CurrentPart && _targetPart != 0)
					continue;
			}

			m_parametersPerPart[_targetPart].emplace_back(*this, param, i, model);
		}
	}
}
