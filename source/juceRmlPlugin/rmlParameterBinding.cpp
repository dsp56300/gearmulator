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

	void RmlParameterBinding::bind(Rml::Element& _element, const std::string& _parameterName, const uint8_t _part/* = CurrentPart*/)
	{
		bind(m_controller, _element, _parameterName, _part);
	}

	void RmlParameterBinding::bind(const pluginLib::Controller& _controller, Rml::Element& _element, const std::string& _parameterName, const uint8_t _part/* = CurrentPart*/)
	{
		auto* p = _controller.getParameter(_parameterName, _part == CurrentPart ? 0 : _part);

		if (!p)
		{
			std::stringstream ss;
			ss << "Failed to find parameter " << _parameterName << " for combo box";
			Rml::Log::Message(_element.GetCoreInstance(), Rml::Log::LT_ERROR, "%s", ss.str().c_str());
			return;
		}

		auto oldParamIt = m_elementToParam.find(&_element);

		if (oldParamIt != m_elementToParam.end())
		{
			evUnbind.invoke(oldParamIt->second, oldParamIt->first);

			m_paramToElements[oldParamIt->second].erase(&_element);
			m_elementToParam.erase(oldParamIt);
		}

		m_elementToParam.insert({ &_element, p });

		m_paramToElements[p].insert(&_element);

		evBind.invoke(p, &_element);

		const auto param = RmlParameterRef::createVariableName(_parameterName);

		const auto paramChanged = juceRmlUi::helper::changeAttribute(&_element, "param", _parameterName);
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-min", param + "_min");
		juceRmlUi::helper::changeAttribute(&_element, "data-attr-max", param + "_max");
		juceRmlUi::helper::changeAttribute(&_element, "data-value", param + "_value");

		std::string modelName = getDataModelName(_part);

		const auto modelChanged = juceRmlUi::helper::changeAttribute(&_element, "data-model", modelName);

		if (auto* combo = dynamic_cast<juceRmlUi::ElemComboBox*>(&_element))
			combo->setOptions(p->getDescription().valueList.texts);

		// determine if we need to rebind at runtime
		if (!modelChanged && !paramChanged)
			return;

		// we need to refresh the data model, unfortunately RmlUi does not provide a way to do this directly.
		// What we do is to remove the element from its parent and reinsert it, which will trigger a refresh of the data model.

		auto* parent = _element.GetParentNode();
		if (!parent)
			return;

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
		return it != m_elementToParam.end() ? it->second : nullptr;
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
}
