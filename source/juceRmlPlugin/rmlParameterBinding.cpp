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

	std::string RmlParameterBinding::getDataModelName(uint8_t _part)
	{
		if (_part == CurrentPart)
			return "partCurrent";
		return "part" + std::to_string(_part);
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

	void RmlParameterBinding::bind(Rml::Element& _element, const std::string& _parameterName, const uint8_t _part/* = CurrentPart*/)
	{
		bind(m_controller, _element, _parameterName, _part);
	}

	void RmlParameterBinding::bind(pluginLib::Controller& _controller, Rml::Element& _element, const std::string& _parameterName, const uint8_t _part/* = CurrentPart*/)
	{
		auto* p = _controller.getParameter(_parameterName, _part == CurrentPart ? 0 : _part);

		if (!p)
		{
			std::stringstream ss;
			ss << "Failed to find parameter " << _parameterName << " for combo box";
			Rml::Log::Message(_element.GetCoreInstance(), Rml::Log::LT_ERROR, "%s", ss.str().c_str());
			return;
		}

		unbind(_element, false);

		BoundParameter bp;
		bp.parameter = p;
		bp.element = &_element;

		if (p->getDescription().isSoftKnob())
		{
			auto* softknob = _controller.getSoftknob(p);
			if (softknob)
			{
				bp.onSoftKnobTargetChanged.set(softknob->onBind, [&_element](const pluginLib::Parameter* _param)
				{
					if (!_param)
						return;

					const auto& range = _param->getNormalisableRange();

					juceRmlUi::helper::changeAttribute(&_element, "data-attr-min", std::to_string(range.start));
					juceRmlUi::helper::changeAttribute(&_element, "data-attr-max", std::to_string(range.end));
					juceRmlUi::helper::changeAttribute(&_element, "data-attr-default", std::to_string(_param->getDefault()));
					juceRmlUi::helper::changeAttribute(&_element, "min", std::to_string(range.start));
					juceRmlUi::helper::changeAttribute(&_element, "max", std::to_string(range.end));
					juceRmlUi::helper::changeAttribute(&_element, "default", std::to_string(_param->getDefault()));
				});
			}
		}
		m_elementToParam.insert(std::make_pair(&_element, std::move(bp)));

		m_paramToElements[p].insert(&_element);

		evBind.invoke(p, &_element);

		const auto param = RmlParameterRef::createVariableName(_parameterName);

		const auto paramChanged = juceRmlUi::helper::changeAttribute(&_element, "param", _parameterName);
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-min", param + "_min");
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-max", param + "_max");
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-default", param + "_default");
		juceRmlUi::helper::changeAttribute(&_element, "data-value", param + "_value");

		std::string modelName = getDataModelName(_part);

		const auto modelChanged = juceRmlUi::helper::changeAttribute(&_element, "data-model", modelName);

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

		// determine if we need to rebind at runtime
		if (!modelChanged && !paramChanged)
			return;

		refreshDataModelForElement(_element);
	}

	void RmlParameterBinding::unbind(Rml::Element& _element, bool _refreshDataModel/* = true*/)
	{
		auto it = m_elementToParam.find(&_element);

		if (it != m_elementToParam.end())
		{
			auto oldParam = it->second.parameter;
			m_paramToElements[oldParam].erase(&_element);
			m_elementToParam.erase(it);

			evUnbind.invoke(oldParam, &_element);
		}

		if (_refreshDataModel)
		{
			_element.RemoveAttribute("param");
			_element.RemoveAttribute("data-attr-min");
			_element.RemoveAttribute("data-attr-max");
			_element.RemoveAttribute("data-attr-default");
			_element.RemoveAttribute("data-value");
			_element.RemoveAttribute("data-model");

			refreshDataModelForElement(_element);
		}
	}

	Rml::Element* RmlParameterBinding::getElementForParameter(const pluginLib::Parameter* _param, const bool _visibleOnly) const
	{
		const auto it = m_paramToElements.find(const_cast<pluginLib::Parameter*>(_param));

		if (it == m_paramToElements.end())
			return {};

		for (auto* elem : it->second)
		{
			if (!_visibleOnly || elem->IsVisible(true))
				return elem;
		}
		return {};
	}

	const pluginLib::Parameter* RmlParameterBinding::getParameterForElement(const Rml::Element* _element) const
	{
		const auto it = m_elementToParam.find(const_cast<Rml::Element*>(_element));
		return it != m_elementToParam.end() ? it->second.parameter : nullptr;
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

	void RmlParameterBinding::registerPendingGesture(RmlParameterRef* _paramRef)
	{
		if (!getMouseIsDown())
			return;
		if (!m_pendingGestures.insert(_paramRef).second)
			return;
		_paramRef->pushGesture();
	}

	void RmlParameterBinding::releasePendingGestures()
	{
		for (const auto* paramRef : m_pendingGestures)
		{
			paramRef->popGesture();
		}
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

			if (param->getDescription().isNonPartSensitive())
			{
				// non-part sensitive parameters are always bound to the current part and to part 0 but not any other part
				if (_targetPart != CurrentPart && _targetPart != 0)
					continue;
			}

			m_parametersPerPart[_targetPart].emplace_back(*this, param, i, model);
		}
	}

	void RmlParameterBinding::refreshDataModelForElement(Rml::Element& _element)
	{
		// we need to refresh the data model, unfortunately RmlUi does not provide a way to do this directly.
		// What we do is to remove the element from its parent and reinsert it, which will trigger a refresh of the data model.

		auto* parent = _element.GetParentNode();
		if (!parent)
			return;	// there is no data model to refresh if the element is not attached to the document

		const auto count = parent->GetNumChildren();
		int childIndex = -1;

		for (int i=0; i<count; ++i)
		{
			auto* e = parent->GetChild(i);
			if (e == &_element)
			{
				childIndex = i;
				break;
			}
		}

		auto* childAfter = parent->GetChild(childIndex + 1);
		auto element = parent->RemoveChild(&_element);
		if (childAfter)
			parent->InsertBefore(std::move(element), childAfter);
		else
			parent->AppendChild(std::move(element));
	}
}
