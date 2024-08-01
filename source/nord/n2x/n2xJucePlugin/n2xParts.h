#pragma once

#include "n2xPart.h"

#include "jucePluginLib/event.h"

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

		std::array<Part*,4> m_parts;
		pluginLib::EventListener<uint8_t> onCurrentPartChanged;

	};
}
