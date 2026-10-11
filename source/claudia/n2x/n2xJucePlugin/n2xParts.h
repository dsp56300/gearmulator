#pragma once

#include <array>
#include <memory>

#include "n2xPart.h"

#include "jucePluginEditorLib/parameterDrivenLed.h"

#include "baseLib/event.h"

namespace n2xJucePlugin
{
	class Editor;

	class Parts
	{
	public:
		explicit Parts(Editor& _editor);

	private:
		void setCurrentPart(uint8_t _part) const;

		Editor& m_editor;

		std::array<std::unique_ptr<Part>,4> m_parts;
		std::array<std::unique_ptr<jucePluginEditorLib::ParameterDrivenLed>,4> m_partLeds;
		baseLib::EventListener<uint8_t> onCurrentPartChanged;

	};
}
