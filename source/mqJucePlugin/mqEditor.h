#pragma once

#include "../juceUiLib/editor.h"

namespace mqJucePlugin
{
	class Editor final : public genericUI::EditorInterface, public genericUI::Editor
	{
	public:
		Editor();
		static const char* findNamedResourceByFilename(const std::string& _filename, uint32_t& _size);
	};
}
