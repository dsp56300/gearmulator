#include "rmlFileInterface.h"

#include <algorithm>

#include "rmlHelper.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "baseLib/filesystem.h"
#include "juceUiLib/editorInterface.h"

namespace juceRmlUi
{
	FileInterface::FileInterface(genericUI::EditorInterface& _editorInterface) : m_editorInterface(_editorInterface)
	{
	}

	Rml::FileHandle FileInterface::Open(const Rml::String& _path)
	{
		uint32_t size;
		auto* data = m_editorInterface.getResourceByFilename(baseLib::filesystem::getFilenameWithoutPath(_path), size);
		if (!data)
			return 0;
		auto* fileData = new FileInfo{ data, size, 0 };
		return helper::toHandle<Rml::FileHandle>(fileData);
	}

	void FileInterface::Close(Rml::FileHandle _file)
	{
		if (_file == 0)
			return;
		auto* file = helper::fromHandle<FileInfo>(_file);
		delete file;
	}

	size_t FileInterface::Read(void* buffer, size_t size, Rml::FileHandle _file)
	{
		if (_file == 0)
			return 0;
		auto* file = helper::fromHandle<FileInfo>(_file);
		if (file->readPos >= file->size)
			return 0;
		const auto bytesToRead = std::min(static_cast<uint32_t>(size), file->size - file->readPos);
		std::memcpy(buffer, file->data + file->readPos, bytesToRead);
		file->readPos += bytesToRead;
		return bytesToRead;
	}

	bool FileInterface::Seek(Rml::FileHandle _file, long _offset, int _origin)
	{
		if (_file == 0)
			return false;
		auto* file = helper::fromHandle<FileInfo>(_file);
		switch (_origin)
		{
		case SEEK_SET:
			file->readPos = _offset;
			break;
		case SEEK_CUR:
			file->readPos += _offset;
			break;
		case SEEK_END:
			file->readPos = file->size + _offset;
			break;
		default:
			return false;
		}
		file->readPos = std::min(file->readPos, file->size);
		return true;
	}

	size_t FileInterface::Tell(const Rml::FileHandle _file)
	{
		if (_file == 0)
			return 0;
		auto* file = helper::fromHandle<FileInfo>(_file);
		return file->readPos;
	}

	size_t FileInterface::Length(const Rml::FileHandle _file)
	{
		if (_file == 0)
			return 0;
		auto* file = helper::fromHandle<FileInfo>(_file);
		return file->size;
	}
}
