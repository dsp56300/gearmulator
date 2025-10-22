#pragma once

#include "baseLib/event.h"

namespace juceRmlUi
{
	class RmlComponent;
}

namespace Rml
{
	class Element;
}

namespace jucePluginEditorLib
{
	class Editor;

	class Led
	{
	public:
		using SourceCallback = std::function<float()>;

		Led(const Editor& _editor, Rml::Element* _targetAlpha, Rml::Element* _targetInvAlpha = nullptr);

		void setValue(float _v);

		void setSourceCallback(SourceCallback&& _func);

	private:
		void repaint() const;

		const Editor& m_editor;

		Rml::Element* m_targetAlpha;
		Rml::Element* m_targetInvAlpha;

		float m_value = -1.0f;

		SourceCallback m_sourceCallback;

		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onPreUpdate;
		baseLib::EventListener<juceRmlUi::RmlComponent*> m_onPostUpdate;
	};
}
