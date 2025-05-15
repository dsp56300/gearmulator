#pragma once

#include "RmlUi/Core/FileInterface.h"

namespace genericUI
{
	class EditorInterface;
}

namespace juceRmlUi
{
	class FileInterface final : public Rml::FileInterface
	{
	public:
		explicit FileInterface(genericUI::EditorInterface& _editorInterface);
		~FileInterface() override;

		Rml::FileHandle Open(const Rml::String& _path) override;
		void Close(Rml::FileHandle _file) override;
		size_t Read(void* buffer, size_t size, Rml::FileHandle _file) override;
		bool Seek(Rml::FileHandle _file, long _offset, int _origin) override;
		size_t Tell(Rml::FileHandle _file) override;
		size_t Length(Rml::FileHandle _file) override;

	private:
		genericUI::EditorInterface& m_editorInterface;

		struct FileInfo
		{
			const char* data = nullptr;
			uint32_t size = 0;
			uint32_t readPos = 0;
		};
	};
}
