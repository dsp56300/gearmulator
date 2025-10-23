#include "rmlPluginDocument.h"

#include "rmlControllerLink.h"
#include "rmlPluginContext.h"
#include "rmlTabGroup.h"

#include "juceRmlUi/rmlElemButton.h"
#include "juceRmlUi/rmlElemKnob.h"
#include "juceRmlUi/rmlEventListener.h"

#include "juceRmlUi/rmlHelper.h"

#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace rmlPlugin
{
	class DocumentListener : public Rml::EventListener
	{
	public:
		DocumentListener(RmlPluginDocument& _doc) : m_document(_doc) {}
		void ProcessEvent(Rml::Event& _event) override
		{
			m_document.processEvent(_event);
		}

		RmlPluginDocument& m_document;
	};

	RmlPluginDocument::RmlPluginDocument(RmlPluginContext& _context) : m_context(_context)
	{
	}

	RmlPluginDocument::~RmlPluginDocument()
	{
		if (m_documentListener)
		{
			m_document->RemoveEventListener(Rml::EventId::Mousedown, m_documentListener.get());
			m_document->RemoveEventListener(Rml::EventId::Mouseup, m_documentListener.get());
			m_document->RemoveEventListener(Rml::EventId::Mouseout, m_documentListener.get());

			m_documentListener.reset();
		}

		m_tabGroups.clear();
		m_controllerLinks.clear();
		m_controllerLinkDescs.clear();
	}

	Rml::Context* RmlPluginDocument::getContext() const
	{
		return m_context.getContext();
	}

	bool RmlPluginDocument::selectTabWithElement(const Rml::Element* _element) const
	{
		for (auto& it : m_tabGroups)
		{
			if (it.second->selectTabWithElement(_element))
				return true;
		}
		return false;
	}

	void RmlPluginDocument::processEvent(const Rml::Event& _event)
	{
		switch (_event.GetId())
		{
		case Rml::EventId::Mousedown:
			if (juceRmlUi::helper::getMouseButton(_event) == juceRmlUi::MouseButton::Left)
				setMouseIsDown(true);
			break;
		case Rml::EventId::Mouseup:
			if (juceRmlUi::helper::getMouseButton(_event) == juceRmlUi::MouseButton::Left)
				setMouseIsDown(false);
			break;
		case Rml::EventId::Mouseout:
			if (_event.GetTargetElement() == m_document)
				setMouseIsDown(false);
			break;
		default:
			break;
		}
	}

	bool RmlPluginDocument::addControllerLink(Rml::Element* _source, Rml::Element* _target, Rml::Element* _conditionButton)
	{
		// check for duplicate links
		for (const auto& link : m_controllerLinks)
		{
			if (link->getSource() == _source && link->getTarget() == _target && link->getConditionButton() == _conditionButton)
				return false; // duplicate link
		}

		m_controllerLinks.emplace_back(std::make_unique<ControllerLink>(_source, _target, _conditionButton));
		return true;
	}

	void RmlPluginDocument::setMouseIsDown(const bool _isDown)
	{
		if (m_mouseIsDown == _isDown)
			return;

		m_mouseIsDown = _isDown;

		m_context.getParameterBinding().setMouseIsDown(m_document, _isDown);
	}

	void RmlPluginDocument::loadCompleted(Rml::ElementDocument* _doc)
	{
		m_document = _doc;

		m_documentListener.reset(new DocumentListener(*this));

		_doc->AddEventListener(Rml::EventId::Mousedown, m_documentListener.get());
		_doc->AddEventListener(Rml::EventId::Mouseup, m_documentListener.get());
		_doc->AddEventListener(Rml::EventId::Mouseout, m_documentListener.get());

		for (const auto & desc : m_controllerLinkDescs)
		{
			auto* target = juceRmlUi::helper::findChild(m_document, desc.target, true);
			auto* button = juceRmlUi::helper::findChild(m_document, desc.conditionButton, true);

			m_controllerLinks.emplace_back(std::make_unique<ControllerLink>(desc.source, target, button));
		}

		m_controllerLinkDescs.clear();
	}

	void RmlPluginDocument::elementCreated(Rml::Element* _element)
	{
		if (auto* attribTabGroup = _element->GetAttribute("tabgroup"))
		{
			const auto name = attribTabGroup->Get<Rml::String>(_element->GetCoreInstance());
			auto& tabGroup = m_tabGroups[name];
			if (!tabGroup)
				tabGroup = std::make_unique<TabGroup>(name, m_context.getContext());
			if (auto* attribPage = _element->GetAttribute("tabpage"))
				tabGroup->setPage(_element, std::stoi(attribPage->Get<Rml::String>(_element->GetCoreInstance())));
			else if (auto* attribButton = _element->GetAttribute("tabbutton"))
				tabGroup->setButton(_element, std::stoi(attribButton->Get<Rml::String>(_element->GetCoreInstance())));
			else
				throw std::runtime_error("tabgroup element must have either tabpage or tabbutton attribute set");
		}

		if (auto* attribLinkTarget = _element->GetAttribute("controllerLinkTarget"))
		{
			const auto target = attribLinkTarget->Get<Rml::String>(_element->GetCoreInstance());
			if (target.empty())
				throw std::runtime_error("controllerLinkTarget attribute must not be empty");

			std::string conditionButton = _element->GetAttribute("controllerLinkCondition", std::string());
			if (conditionButton.empty())
				throw std::runtime_error("controllerLinkCondition attribute must not be empty");

			m_controllerLinkDescs.push_back({ _element, target, conditionButton });
		}

		if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(_element))
		{
			if (input->GetAttribute("type", std::string("")) == "range")
			{
				// reset sliders to their default value on double click

				juceRmlUi::EventListener::Add(input, Rml::EventId::Dblclick, [this, input](const Rml::Event&)
				{
					auto& binding = m_context.getParameterBinding();
					auto* parameter = binding.getParameterForElement(input);

					// if they are bound to parameter, use the parameter. This is because the mapping might be reversed
					if (parameter)
					{
						parameter->setUnnormalizedValueNotifyingHost(parameter->getDefault(), pluginLib::Parameter::Origin::Ui);
					}
					else
					{
						// otherwise, just use the default attribute if available
						auto defaultValue = input->GetAttribute("default", std::string());
						if (!defaultValue.empty())
							input->SetValue(defaultValue);
					}
				});

				// allow to change slider value with mouse wheel
				juceRmlUi::EventListener::Add(input, Rml::EventId::Mousescroll, [input](const Rml::Event& _event)
				{
					juceRmlUi::ElemKnob::processMouseWheel(*input, _event);
				});
			}
		}

		// if an element has a "url" attribute with an http(s) link, open that link in the default browser when the element is clicked
		if (auto* attrib = _element->GetAttribute("url"))
		{
			const auto url = attrib->Get<Rml::String>(_element->GetCoreInstance());

			if (url.size() > 4 && url.substr(0,4) == "http")
			{
				juceRmlUi::EventListener::Add(_element, Rml::EventId::Click, [url](const Rml::Event&)
				{
					juce::URL(url).launchInDefaultBrowser();
				});
			}
		}

		if (auto* button = dynamic_cast<juceRmlUi::ElemButton*>(_element))
		{
			if (button->getValueOn() >= 0 && button->getValueOff() < 0)
			{
				// if a button has only a value-on attribute, it might be part of a radio button group. We want to scroll
				// through the buttons in that group with the mouse wheel
				juceRmlUi::EventListener::Add(button, Rml::EventId::Mousescroll, [button, this](const Rml::Event& _event)
				{
					const auto isUp = juceRmlUi::helper::isMouseWheelUp(_event);
					const auto isDown = juceRmlUi::helper::isMouseWheelDown(_event);

					if (!isUp && !isDown)
						return;

					auto& binding = m_context.getParameterBinding();
					auto* param = binding.getParameterForElement(button);
					if (!param)
						return;

					// retrieve all buttons for that parameter that are radio buttons too
					std::vector<Rml::Element*> elems;
					binding.getElementsForParameter(elems, param, true);

					juceRmlUi::ElemButton* checkedButton = nullptr;

					for (int i=0; i<static_cast<int>(elems.size()); ++i)
					{
						auto* b = dynamic_cast<juceRmlUi::ElemButton*>(elems[i]);

						if (!b || b->getValueOn() < 0 || b->getValueOff() >= 0)
						{
							elems.erase(elems.begin() + i);
							--i;
						}
						else if (b->isChecked())
						{
							checkedButton = b;
							elems.erase(elems.begin() + i);
							--i;
						}
					}

					if (!checkedButton)
						return;

					auto getCoordValue = [](Rml::Element* _e)
					{
						return _e->GetAbsoluteTop() + _e->GetAbsoluteLeft();
					};

					// find the next button along the Y axis, depending on scroll direction
					const auto buttonTop = getCoordValue(checkedButton);

					std::sort(elems.begin(), elems.end(), [getCoordValue, isDown](Rml::Element* _a, Rml::Element* _b)
					{
						if (isDown)
							return getCoordValue(_a) < getCoordValue(_b);
						return getCoordValue(_a) > getCoordValue(_b);
					});

					const auto it = std::lower_bound(elems.begin(), elems.end(), buttonTop, [isDown, getCoordValue](Rml::Element* _a, const float _b)
					{
						if (isDown)
							return getCoordValue(_a) < _b;
						return getCoordValue(_a) > _b;
					});

					if (it == elems.end())
						return;

					// if we found a button, set it
					auto* b = dynamic_cast<juceRmlUi::ElemButton*>(*it);
					if (b)
						b->setChecked(true);
				});
			}
		}
	}
}
