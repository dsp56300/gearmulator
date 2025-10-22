#include "search.h"

#include <algorithm>

#include "dsp56kEmu/logging.h"

#include "juceRmlUi/rmlEventListener.h"

#include "RmlUi/Core/Elements/ElementFormControlInput.h"

namespace jucePluginEditorLib::patchManagerRml
{
	Search::Search(Rml::ElementFormControlInput* _input)
	{
		juceRmlUi::EventListener::Add(_input, Rml::EventId::Change, [this, _input](Rml::Event&)
		{
			setText(_input->GetValue());
		});
	}

	std::string Search::lowercase(const std::string& _s)
	{
		auto t = _s;
		std::transform(t.begin(), t.end(), t.begin(), tolower);
		return t;
	}

	void Search::onTextChanged(const std::string& _text)
	{
	}

	void Search::setText(const std::string& _text)
	{
		const auto t = lowercase(_text);

		if (m_text == t)
			return;

		m_text = t;
		onTextChanged(t);
	}
}
