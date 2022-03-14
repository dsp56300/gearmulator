#include "VirusEditor.h"

#include "BinaryData.h"

namespace genericVirusUI
{
	VirusEditor::VirusEditor(VirusParameterBinding& _binding, Virus::Controller& _controller) : Editor(std::string(BinaryData::VirusC_json, BinaryData::VirusC_jsonSize), _binding, _controller),
		m_parts(*this),
		m_tabs(*this)
	{
	}
}
