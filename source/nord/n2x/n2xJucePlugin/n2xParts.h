#pragma once

namespace n2xJucePlugin
{
	class Editor;

	class Parts
	{
	public:
		explicit Parts(Editor& _editor);

	private:
		Editor& m_editor;
	};
}
