#pragma once

#include <cstdint>
#include <map>

#include "jeLib/jemiditypes.h"

namespace Rml
{
	class Event;
	class EventListener;
}

namespace pluginLib
{
	class Parameter;
}

namespace juceRmlUi
{
	class ElemButton;
}

namespace Rml
{
	class Element;
}

namespace jeJucePlugin
{
	class Editor;

	class JeAssign
	{
	public:
		enum class AssignType : uint8_t
		{
			None = 0,
			Velocity,
			Control,
			Count
		};

		JeAssign(const Editor& _editor);

	private:
		void setAssignType(AssignType _type);
		pluginLib::Parameter* getParameter(jeLib::Patch _type) const;
		void onClick(Rml::Event& _event, const juceRmlUi::ElemButton* _button, AssignType _type);

		const Editor& m_editor;

		juceRmlUi::ElemButton* m_btAssignVelocity;
		juceRmlUi::ElemButton* m_btAssignControl;

		AssignType m_assignType = AssignType::None;

		std::map<jeLib::Patch, Rml::Element*> m_controls;
	};
}
