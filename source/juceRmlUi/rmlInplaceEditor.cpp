#include "rmlInplaceEditor.h"

#include <cassert>

#include "rmlEventListener.h"
#include "rmlHelper.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace juceRmlUi
{
	InplaceEditor::InplaceEditor(Rml::Element* _parent, const std::string& _initialValue, ChangeCallback _changeCallback)
	: m_initialValue(_initialValue)
	, m_changeCallback(std::move(_changeCallback))
	{
		_parent->SetProperty(Rml::PropertyId::Position, Rml::Style::Position::Relative);

		auto input = _parent->GetOwnerDocument()->CreateElement("input");

		input->SetAttribute("type", "text");
//		input->SetAttribute("maxlength", "32");

		input->SetProperty(Rml::PropertyId::Display, Rml::Style::Display::Inline);
		input->SetProperty(Rml::PropertyId::Position, Rml::Style::Position::Absolute);
//		input->SetProperty(Rml::PropertyId::BoxSizing, Rml::Style::BoxSizing::BorderBox);

		input->SetProperty(Rml::PropertyId::Left, Rml::Property(0, Rml::Unit::PX));
		input->SetProperty(Rml::PropertyId::Top, Rml::Property(0, Rml::Unit::PX));

		input->SetProperty(Rml::PropertyId::Width, Rml::Property(100, Rml::Unit::PERCENT));
		input->SetProperty(Rml::PropertyId::Height, Rml::Property(100, Rml::Unit::PERCENT));

		m_input = dynamic_cast<Rml::ElementFormControlInput*>(_parent->AppendChild(std::move(input)));
		assert(m_input);

		m_input->SetValue(_initialValue);

		EventListener::Add(m_input, Rml::EventId::Submit, [this](Rml::Event&) { onSubmit(); });
		EventListener::Add(m_input, Rml::EventId::Blur, [this](Rml::Event&) { onBlur(); });
		EventListener::Add(m_input, Rml::EventId::Change, [this](const Rml::Event& _event) { onChange(_event); });
		EventListener::Add(m_input, Rml::EventId::Keydown, [this](Rml::Event& _event) { onKeyDown(_event); });

		m_input->Focus(true);
		m_input->SetSelectionRange(0, static_cast<int>(_initialValue.size()));
	}

	InplaceEditor::~InplaceEditor()
	{
		deleteInputElement();
	}

	void InplaceEditor::onSubmit()
	{
		const auto v = m_input->GetValue();

		deleteInputElement();

		if (v != m_initialValue)
			m_changeCallback(v);

		close();
	}

	void InplaceEditor::onBlur()
	{
		close();
	}

	void InplaceEditor::onChange(const Rml::Event& _event)
	{
		if (_event.GetParameter("linebreak", false))
			onSubmit();
	}

	void InplaceEditor::onKeyDown(Rml::Event& _event)
	{
		const auto key = juceRmlUi::helper::getKeyIdentifier(_event);

		if (key == Rml::Input::KeyIdentifier::KI_ESCAPE)
		{
			_event.StopPropagation();
			close();
		}
	}

	void InplaceEditor::deleteInputElement()
	{
		if (m_input == nullptr)
			return;
		auto* input = m_input;
		m_input = nullptr;
		input->GetParentNode()->RemoveChild(input);
	}

	void InplaceEditor::close()
	{
		if (!m_input)
			return;
		deleteInputElement();
		delete this;
	}
}
