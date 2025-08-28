#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "mqPartButton.h"

namespace Rml
{
	class Element;
}

namespace mqJucePlugin
{
	class Controller;
	class Editor;

	class mqPartSelect
	{
	public:
		explicit mqPartSelect(Editor& _editor, Controller& _controller);
		~mqPartSelect();

		mqPartSelect(mqPartSelect&&) = delete;
		mqPartSelect(const mqPartSelect&) = delete;
		mqPartSelect& operator = (mqPartSelect&&) = delete;
		mqPartSelect& operator = (const mqPartSelect&) = delete;

		void onPlayModeChanged() const;

		void selectPart(uint8_t _index) const;

	private:
		void updateUiState() const;

		struct Part
		{
			std::unique_ptr<mqPartButton> button = nullptr;
			Rml::Element* led = nullptr;
		};

		mqJucePlugin::Editor& m_editor;
		Controller& m_controller;
		std::array<Part, 16> m_parts{};
	};
}
