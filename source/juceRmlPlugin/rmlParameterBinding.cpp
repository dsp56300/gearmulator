#include "rmlParameterBinding.h"

#include "rmlParameterRef.h"

#include "jucePluginLib/parameterdescriptions.h"

#include "juceRmlUi/juceRmlComponent.h"
#include "juceRmlUi/rmlDataProvider.h"
#include "juceRmlUi/rmlElemComboBox.h"
#include "juceRmlUi/rmlHelper.h"
#include "juceRmlUi/rmlInterfaces.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/DataModelHandle.h"

namespace rmlPlugin
{
	RmlParameterBinding::RmlParameterBinding(pluginLib::Controller& _controller, Rml::Context* _context, juceRmlUi::RmlComponent& _component)
	: m_controller(_controller)
	, m_component(_component)
	{
		m_onCurrentPartChanged.set(_controller.onCurrentPartChanged, [this](const uint8_t _part)
		{
			setCurrentPart(_part);
		});

		bindParameters(_context, 16);
	}

	RmlParameterBinding::~RmlParameterBinding()
	{
		for (auto& [param, boundParam] : m_paramToElements)
			delete boundParam;
		m_paramToElements.clear();
	}

	std::string RmlParameterBinding::getDataModelName(const uint8_t _part)
	{
		if (_part == CurrentPart)
			return "partCurrent";
		return "part" + std::to_string(_part);
	}

	uint8_t RmlParameterBinding::getPartFromDataModelName(const std::string& _name)
	{
		if (_name == "partCurrent")
			return CurrentPart;

		if (_name.size() > 4 && _name.substr(0, 4) == "part")
		{
			try
			{
				const auto part = std::stoul(_name.substr(4));
				return static_cast<uint8_t>(part);
			}
			catch (...)
			{
				return CurrentPart;
			}
		}
		return CurrentPart;
	}

	void RmlParameterBinding::bindParameters(Rml::Context* _context, const uint8_t _partCount)
	{
		for (uint8_t part = 0; part < _partCount; ++part)
			bindParametersForPart(_context, part, part);

		bindParametersForPart(_context, CurrentPart, m_controller.getCurrentPart());
	}

	Rml::CoreInstance& RmlParameterBinding::getCoreInstance() const
	{
		return m_component.getContext()->GetCoreInstance();
	}

	bool RmlParameterBinding::bind(Rml::Element& _element, const std::string& _parameterName)
	{
		// determine part from data model

		Rml::Element* elem = &_element;

		while (elem)
		{
			const auto model = elem->GetAttribute("data-model", std::string());
			if (!model.empty())
			{
				const auto part = getPartFromDataModelName(model);
				bind(_element, _parameterName, part);
				return true;
			}
			elem = elem->GetParentNode();
		}
		return false;
	}

	void RmlParameterBinding::bind(Rml::Element& _element, const std::string& _parameterName, const uint8_t _part/* = CurrentPart*/)
	{
		auto* p = m_controller.getParameter(_parameterName, _part == CurrentPart ? 0 : _part);

		if (!p)
		{
			std::stringstream ss;
			ss << "Failed to find parameter " << _parameterName << " for combo box";
			Rml::Log::Message(_element.GetCoreInstance(), Rml::Log::LT_ERROR, "%s", ss.str().c_str());
			return;
		}

		unbind(_element);

		ParameterToElementsBinding* bp;

		auto it = m_paramToElements.find(p);

		if (it == m_paramToElements.end())
		{
			bp = new ParameterToElementsBinding(*this, p, &_element);
			m_paramToElements.insert({ p, bp });
		}
		else
		{
			bp = it->second;
			bp->addElement(&_element);
		}

		m_elementToParam.insert(std::make_pair(&_element, bp));

		evBind.invoke(p, &_element);

		const auto param = RmlParameterRef::createVariableName(_parameterName);

		std::string modelName = getDataModelName(_part);

		if (auto* combo = dynamic_cast<juceRmlUi::ElemComboBox*>(&_element))
		{
			std::vector<juceRmlUi::ElemComboBox::Entry> sortedValues;

			const auto& desc = p->getDescription();
			const auto& valueList = desc.valueList;
			const auto& range = desc.range;

			if(valueList.order.empty())
			{
				int i = 0;
				const auto& allValues = p->getAllValueStrings();
				for (const auto& vs : allValues)
				{
					if(vs.isNotEmpty() && i >= range.getStart() && i <= range.getEnd())
						sortedValues.emplace_back(juceRmlUi::ElemComboBox::Entry{ vs.toStdString(), i });
					++i;
				}
			}
			else
			{
				for(uint32_t i=0; i<valueList.order.size(); ++i)
				{
					const auto value = valueList.orderToValue(i);
					if(value == pluginLib::ValueList::InvalidValue)
						continue;
					if(value < range.getStart() || value > range.getEnd())
						continue;
					const auto text = valueList.valueToText(value);
					if(text.empty())
						continue;
					sortedValues.emplace_back(juceRmlUi::ElemComboBox::Entry{ text, value });
				}
			}
			combo->setEntries(sortedValues);
		}
	}

	void RmlParameterBinding::unbind(Rml::Element& _element)
	{
		auto it = m_elementToParam.find(&_element);

		if (it != m_elementToParam.end())
		{
			auto oldParam = it->second->getParameter();

			auto* bp = m_paramToElements.find(oldParam)->second;

			bp->removeElement(&_element);

			if (bp->empty())
			{
				delete bp;
				m_paramToElements.erase(oldParam);
			}

			m_elementToParam.erase(it);

			evUnbind.invoke(oldParam, &_element);
		}
	}

	void RmlParameterBinding::getElementsForParameter(std::vector<Rml::Element*>& _results, const std::string& _param, const uint8_t _part, const bool _visibleOnly) const
	{
		auto* p = m_controller.getParameter(_param, _part);
		if (!p)
			return;
		getElementsForParameter(_results, p, _visibleOnly);
	}

	void RmlParameterBinding::getElementsForParameter(std::vector<Rml::Element*>& _results, const pluginLib::Parameter* _param, bool _visibleOnly) const
	{
		const auto it = m_paramToElements.find(const_cast<pluginLib::Parameter*>(_param));

		if (it == m_paramToElements.end())
			return;

		for (auto* elem : it->second->getElements())
		{
			if (!_visibleOnly || elem->IsVisible(true))
				_results.push_back(elem);
		}
	}

	Rml::Element* RmlParameterBinding::getElementForParameter(const pluginLib::Parameter* _param, const bool _visibleOnly) const
	{
		const auto it = m_paramToElements.find(const_cast<pluginLib::Parameter*>(_param));

		if (it == m_paramToElements.end())
			return {};

		for (auto* elem : it->second->getElements())
		{
			if (!_visibleOnly || elem->IsVisible(true))
				return elem;
		}
		return {};
	}

	Rml::Element* RmlParameterBinding::getElementForParameter(const std::string& _param, const uint8_t _part, const bool _visibleOnly) const
	{
		auto* p = m_controller.getParameter(_param, _part);
		if (!p)
			return {};
		return getElementForParameter(p, _visibleOnly);
	}

	const pluginLib::Parameter* RmlParameterBinding::getParameterForElement(const Rml::Element* _element) const
	{
		const auto it = m_elementToParam.find(const_cast<Rml::Element*>(_element));
		return it != m_elementToParam.end() ? it->second->getParameter() : nullptr;
	}

	void RmlParameterBinding::setMouseIsDown(Rml::ElementDocument* _document, bool _isDown)
	{
		const auto mouseWasDown = getMouseIsDown();

		if (_isDown)
			m_docsWithMouseDown.insert(_document);
		else
			m_docsWithMouseDown.erase(_document);

		const auto mouseIsDown = getMouseIsDown();

		if (mouseWasDown == mouseIsDown)
			return;

		if (!mouseIsDown)
			releasePendingGestures();
	}

	void RmlParameterBinding::registerPendingGesture(pluginLib::Parameter* _param)
	{
		if (!getMouseIsDown())
			return;
		if (!m_pendingGestures.insert(_param).second)
			return;

		_param->pushChangeGesture();
	}

	void RmlParameterBinding::releasePendingGestures()
	{
		for (auto* paramRef : m_pendingGestures)
			paramRef->popChangeGesture();

		m_pendingGestures.clear();
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

		auto model = _context->CreateDataModel(getDataModelName(_targetPart));

		m_parametersPerPart[_targetPart].reserve(descs.size());

		for (uint32_t i=0; i<static_cast<uint32_t>(descs.size()); ++i)
		{
			auto* param = m_controller.getParameter(i, _sourcePart);

			// we silenty fail here because _sourcePart might be out of range, that is no issue
			if (!param)
				continue;

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
