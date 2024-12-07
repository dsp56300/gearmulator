#include "filetype.h"

#include "synthLib/os.h"

namespace jucePluginEditorLib
{
	FileType FileType::Syx("syx");
	FileType FileType::Mid("mid");

	bool FileType::operator==(const FileType& _other) const
	{
		return synthLib::lowercase(type) == synthLib::lowercase(_other.type);
	}

	bool FileType::operator!=(const FileType& _other) const
	{
		return !(*this == _other);
	}
}
