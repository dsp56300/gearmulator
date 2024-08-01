#pragma once

namespace n2xJucePlugin
{
	class Editor;

	class Arp
	{
	public:
		explicit Arp(Editor& _editor);

	private:
		Editor& m_editor;
	};
}