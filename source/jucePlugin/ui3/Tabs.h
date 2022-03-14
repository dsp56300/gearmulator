#pragma once

namespace genericVirusUI
{
	class Tabs
	{
	public:
		Tabs(VirusEditor& _editor) : m_editor(_editor) {}
	private:
		VirusEditor& m_editor;
	};
}