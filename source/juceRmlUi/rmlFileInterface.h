#pragma once

#include "RmlUi/Core/FileInterface.h"

namespace juceRmlUi
{
	class DataProvider;

	class FileInterface final : public Rml::FileInterface
	{
	public:
		explicit FileInterface(Rml::CoreInstance& _coreInstance, DataProvider& _dataProvider);
		~FileInterface() override = default;
		FileInterface(const FileInterface&) = delete;
		FileInterface(FileInterface&&) = delete;

		FileInterface& operator=(const FileInterface&) = delete;
		FileInterface& operator=(FileInterface&&) = delete;

		Rml::FileHandle Open(const Rml::String& _path) override;
		void Close(Rml::FileHandle _file) override;
		size_t Read(void* buffer, size_t size, Rml::FileHandle _file) override;
		bool Seek(Rml::FileHandle _file, long _offset, int _origin) override;
		size_t Tell(Rml::FileHandle _file) override;
		size_t Length(Rml::FileHandle _file) override;

	private:
		DataProvider& m_dataProvider;

		struct FileInfo
		{
			const void* data = nullptr;
			uint32_t size = 0;
			uint32_t readPos = 0;
		};
	};
}
