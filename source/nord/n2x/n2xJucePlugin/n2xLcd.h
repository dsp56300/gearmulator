#pragma once

namespace n2xJucePlugin
{
	class Editor;

	class Lcd
	{
	public:
		explicit Lcd(Editor& _editor);

	private:
		Editor& m_editor;
	};
}