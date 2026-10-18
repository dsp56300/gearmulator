#include "rmlTooltips.h"

#include <algorithm>

#include "juceRmlComponent.h"

#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/StringUtilities.h"
#include "RmlUi/Core/SystemInterface.h"

namespace juceRmlUi
{
	namespace
	{
		constexpr float g_gapDp = 4.0f;

		Rml::Element* findTitled(Rml::Element* _element)
		{
			for (auto* e = _element; e; e = e->GetParentNode())
			{
				if (!e->GetAttribute<Rml::String>("title", {}).empty())
					return e;
			}
			return nullptr;
		}
	}

	Tooltips::Tooltips(RmlComponent& _component, Rml::ElementDocument& _document, const double _delaySeconds)
		: m_component(_component)
		, m_document(_document.GetObserverPtr(_document.GetCoreInstance()))
		, m_delaySeconds(_delaySeconds)
	{
		_document.AddEventListener(Rml::EventId::Mousedown, this, true);

		m_onPreUpdate.set(_component.evPreUpdate, [this](RmlComponent*)
		{
			onPreUpdate();
		});
	}

	Tooltips::~Tooltips()
	{
		m_onPreUpdate.reset();

		if (auto* doc = m_document.get())
		{
			doc->RemoveEventListener(Rml::EventId::Mousedown, this, true);

			if (auto* tooltip = m_tooltip.get())
			{
				if (auto* parent = tooltip->GetParentNode())
					parent->RemoveChild(tooltip);
			}
		}
	}

	void Tooltips::ProcessEvent(Rml::Event& _event)
	{
		if (_event.GetId() != Rml::EventId::Mousedown)
			return;

		hide();
		m_dismissed = true;
	}

	void Tooltips::onPreUpdate()
	{
		auto* doc = m_document.get();
		auto* context = doc ? doc->GetContext() : nullptr;

		if (!context)
			return;

		auto* source = findTitled(context->GetHoverElement());

		const auto now = doc->GetCoreInstance().system_interface->GetElapsedTime();

		if (source != m_source.get())
		{
			hide();
			m_source = source ? source->GetObserverPtr(doc->GetCoreInstance()) : Rml::ObserverPtr<Rml::Element>();
			m_since = now;
			m_dismissed = false;
		}

		if (!source || m_dismissed)
			return;

		const auto wait = m_since + m_delaySeconds - now;

		if (wait > 0.0)
		{
			context->RequestNextUpdate(wait);
			return;
		}

		const auto text = source->GetAttribute<Rml::String>("title", {});

		if (text != m_text || !m_tooltip.get() || !m_tooltip->IsVisible())
			show(text);
	}

	void Tooltips::show(const std::string& _text)
	{
		auto* doc = dynamic_cast<Rml::ElementDocument*>(m_document.get());
		auto* source = m_source.get();
		auto* context = doc ? doc->GetContext() : nullptr;

		if (!source || !context)
			return;

		auto* tooltip = m_tooltip.get();

		if (!tooltip)
		{
			auto elem = doc->CreateElement("div");
			elem->SetClass("tooltip", true);
			tooltip = doc->AppendChild(std::move(elem), true);
			m_tooltip = tooltip->GetObserverPtr(doc->GetCoreInstance());
		}

		m_text = _text;

		// Placed in pixels, like a menu: what it is laid out against is the document, not the element.
		tooltip->SetProperty("position", "absolute");
		tooltip->SetProperty("pointer-events", "none");
		tooltip->SetProperty("left", "0px");
		tooltip->SetProperty("top", "0px");
		tooltip->SetProperty("display", "block");
		tooltip->SetInnerRML(Rml::StringUtilities::EncodeRml(_text));

		// Laid out once now, so that its size is known before anything is drawn.
		context->Update();

		const auto dims = Rml::Vector2f(context->GetDimensions());
		const auto gap = g_gapDp * context->GetDensityIndependentPixelRatio();

		const auto offset = source->GetAbsoluteOffset(Rml::BoxArea::Border);
		const auto size = source->GetBox().GetSize(Rml::BoxArea::Border);
		const auto tipSize = tooltip->GetBox().GetSize(Rml::BoxArea::Border);

		// Centred under the element, or over it where there is no room below, and kept inside the window.
		auto x = offset.x + (size.x - tipSize.x) * 0.5f;
		auto y = offset.y + size.y + gap;

		if (y + tipSize.y > dims.y)
			y = offset.y - gap - tipSize.y;

		x = std::clamp(x, 0.0f, std::max(0.0f, dims.x - tipSize.x));
		y = std::max(0.0f, y);

		tooltip->SetProperty("left", std::to_string(x) + "px");
		tooltip->SetProperty("top", std::to_string(y) + "px");

		m_component.enqueueUpdate();
	}

	void Tooltips::hide()
	{
		m_text.clear();

		auto* tooltip = m_tooltip.get();

		if (!tooltip || !tooltip->IsVisible())
			return;

		tooltip->SetProperty("display", "none");
		m_component.enqueueUpdate();
	}
}
