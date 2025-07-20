#include "rmlDragTarget.h"

#include "juceRmlComponent.h"
#include "rmlDragData.h"
#include "rmlElemListEntry.h"
#include "rmlEventListener.h"
#include "rmlHelper.h"
#include "RmlUi/Core/Context.h"

#include "RmlUi/Core/Element.h"

namespace juceRmlUi
{
	DragTarget::KeyEventListener::KeyEventListener(DragTarget& _target) : m_target(_target), m_rootElement(_target.m_element->GetContext()->GetRootElement())
	{
		m_rootElement->AddEventListener(Rml::EventId::Keydown, this);
		m_rootElement->AddEventListener(Rml::EventId::Keyup, this);
		m_rootElement->AddEventListener(Rml::EventId::Textinput, this);
	}

	DragTarget::KeyEventListener::~KeyEventListener()
	{
		m_rootElement->RemoveEventListener(Rml::EventId::Keydown, this);
		m_rootElement->RemoveEventListener(Rml::EventId::Keyup, this);
		m_rootElement->RemoveEventListener(Rml::EventId::Textinput, this);
	}

	void DragTarget::KeyEventListener::ProcessEvent(Rml::Event& _event)
	{
		m_target.onKeyChange(_event);
	}

	DragTarget::DragTarget(Rml::Element* _elem)
	{
		if (_elem)
			init(_elem);
	}

	DragTarget::~DragTarget()
	{
		if (m_element)
			m_element->RemoveAttribute("dragTarget");

		m_keyEventListener.reset();
	}

	bool DragTarget::init(Rml::Element* _elem)
	{
		if (m_element)
			return false;

		m_element = _elem;

		EventListener::Add(_elem, Rml::EventId::Dragover, [this](const Rml::Event& _event) { onDragOver(_event); });
		EventListener::Add(_elem, Rml::EventId::Dragmove, [this](const Rml::Event& _event) { onDragMove(_event); });
		EventListener::Add(_elem, Rml::EventId::Dragout, [this](const Rml::Event& _event) { onDragOut(_event); });
		EventListener::Add(_elem, Rml::EventId::Dragdrop, [this](const Rml::Event& _event) { onDragDrop(_event); });

		setAllowLocations(false, true);

		m_element->SetAttribute("dragTarget", this);

		return true;
	}

	bool DragTarget::canDrop(const Rml::Event& _event, const DragSource* _source) const
	{
		if (_source == nullptr)
			return false;

		auto fileDragData = dynamic_cast<FileDragData*>(_source->getDragData());

		if (fileDragData)
		{
			if (fileDragData->files.empty())
				return false;

			return canDropFiles(_event, fileDragData->files);
		}

		return true;
	}

	bool DragTarget::canDropFiles(const Rml::Event&, const std::vector<std::string>&) const
	{
		// to be implemented by derived class
		return false;
	}

	DragTarget* DragTarget::fromElement(const Rml::Element* _elem)
	{
		if (!_elem)
			return nullptr;
		return static_cast<DragTarget*>(_elem->GetAttribute<void*>("dragTarget", nullptr));
	}

	void DragTarget::onDragOver(const Rml::Event& _event)
	{
		if (_event.GetTargetElement() != _event.GetCurrentElement())
			return;

		auto* dragSource = helper::getDragSource(_event);
		if (!dragSource)
			return;

		if (!canDrop(_event, dragSource))
			return;

		m_currentDragSource = dragSource;

		updateDragLocation(_event);

		if (!m_keyEventListener)
			m_keyEventListener.reset(new KeyEventListener(*this));
	}

	void DragTarget::onDragMove(const Rml::Event& _event)
	{
		if (!m_currentDragSource)
			return;
		updateDragLocation(_event);
	}

	void DragTarget::onDragOut(const Rml::Event& _event)
	{
		if (_event.GetTargetElement() != _event.GetCurrentElement())
			return;
		stopDrag();
	}

	void DragTarget::onDragDrop(const Rml::Event& _event)
	{
		if (!m_currentDragSource)
			return;

		if (!canDrop(_event, m_currentDragSource))
		{
			stopDrag();
			return;
		}

		const auto* data = m_currentDragSource->getDragData();
		if (!data)
		{
			stopDrag();
			return;
		}

		auto* fileDragData = dynamic_cast<const FileDragData*>(data);

		if (fileDragData && !fileDragData->files.empty())
			dropFiles(_event, fileDragData, fileDragData->files);
		else
			drop(_event, data);

		stopDrag();
	}

	void DragTarget::onKeyChange(const Rml::Event& _event)
	{
		if (!m_currentDragSource)
			return;
		updateDragLocation(_event);
	}

	void DragTarget::stopDrag()
	{
		if (!m_currentDragSource)
			return;
		m_currentDragSource = nullptr;
		updatePseudoClass({});
		updateShiftPseudoClass(false);
		m_currentLocationH = DragLocation::None;
		m_currentLocationV = DragLocation::None;
		m_keyEventListener.reset();
	}

	void DragTarget::updateDragLocation(const Rml::Event& _event)
	{
		auto* elem = dynamic_cast<Rml::Element*>(this);
		if (!elem)
			return;

		const auto shift = m_allowShift && helper::getKeyModShift(_event);

		updateShiftPseudoClass(shift);

		auto mousePos = helper::getMousePos(_event);
		
		const auto box = elem->GetBox();
		const auto size = box.GetSize(Rml::BoxArea::Padding);
		const auto pos = elem->GetAbsoluteOffset(Rml::BoxArea::Padding);

		const auto thresholdTopLeft = size / 3.0f;
		const auto thresholdBottomRight = size * 2.0f / 3.0f;

		mousePos -= pos;

		auto locationH = DragLocation::Center;

		if (m_allowLocationHorizontal)
		{
			if (mousePos.x < thresholdTopLeft.x)
				locationH = DragLocation::Left;
			else if (mousePos.x >= thresholdBottomRight.x)
				locationH = DragLocation::Right;
		}

		auto locationV = DragLocation::Center;

		if (m_allowLocationVertical)
		{
			if (mousePos.y < thresholdTopLeft.y)
				locationV = DragLocation::Top;
			else if (mousePos.y >= thresholdBottomRight.y)
				locationV = DragLocation::Bottom;
		}

		if (m_currentLocationH != locationH || m_currentLocationV != locationV)
		{
			m_currentLocationH = locationH;
			m_currentLocationV = locationV;
			updatePseudoClass(locationH, locationV);
		}
	}

	void DragTarget::updatePseudoClass(const DragLocation _locationH, const DragLocation _locationV)
	{
		const char* pseudoClass = nullptr;

		switch (_locationH)
		{
		case DragLocation::Left:
			switch (_locationV)
			{
			case DragLocation::Top:
				pseudoClass = "lefttop";
				break;
			case DragLocation::Center:
				pseudoClass = "leftcenter";
				break;
			case DragLocation::Bottom:
				pseudoClass = "leftbottom";
				break;
			case DragLocation::None:
				break;
			}
			break;
		case DragLocation::Center:
			switch (_locationV)
			{
			case DragLocation::Top:
				pseudoClass = "top";
				break;
			case DragLocation::Center:
				pseudoClass = "center";
				break;
			case DragLocation::Bottom:
				pseudoClass = "bottom";
				break;
			case DragLocation::None:
				break;
			}
			break;
		case DragLocation::Right:
			switch (_locationV)
			{
			case DragLocation::Top:
				pseudoClass = "righttop";
				break;
			case DragLocation::Center:
				pseudoClass = "rightcenter";
				break;
			case DragLocation::Bottom:
				pseudoClass = "rightbottom";
				break;
			case DragLocation::None:
				break;
			}
			break;
		case DragLocation::None:
			break;
		}

		if (pseudoClass)
			updatePseudoClass(std::string("dragover") + pseudoClass);
		else
			updatePseudoClass({});
	}

	void DragTarget::updatePseudoClass(const std::string& _pseudoClass)
	{
		auto* elem = dynamic_cast<Rml::Element*>(this);

		if (!elem)
			return;

		if (m_currentLocationPseudoClass == _pseudoClass)
			return;

		if (!m_currentLocationPseudoClass.empty())
			elem->SetPseudoClass(m_currentLocationPseudoClass, false);

		m_currentLocationPseudoClass = _pseudoClass;

		if (!m_currentLocationPseudoClass.empty())
			elem->SetPseudoClass(m_currentLocationPseudoClass, true);
	}

	void DragTarget::updateShiftPseudoClass(bool _shift)
	{
		if (m_shiftDown == _shift)
			return;

		m_shiftDown = _shift;

		auto* elem = dynamic_cast<Rml::Element*>(this);
		if (!elem)
			return;
		if (m_shiftDown)
			elem->SetPseudoClass("shift", true);
		else
			elem->SetPseudoClass("shift", false);
	}
}
