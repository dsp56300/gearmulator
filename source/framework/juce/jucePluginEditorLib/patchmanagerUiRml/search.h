#pragma once

#include <string>

namespace Rml
{
	class ElementFormControlInput;
}

namespace jucePluginEditorLib::patchManagerRml
{
	class Search
	{
	public:
		Search(Rml::ElementFormControlInput* _input);

		static std::string lowercase(const std::string& _s);

		virtual void onTextChanged(const std::string& _text);

		const std::string& getSearchText() const { return m_text; }

	private:
		void setText(const std::string& _text);

		std::string m_text;
	};
}
