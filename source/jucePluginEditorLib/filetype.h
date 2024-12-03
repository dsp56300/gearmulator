#pragma once

#include <string>

namespace jucePluginEditorLib
{
	class FileType
	{
	public:
		static FileType Syx;
		static FileType Mid;

		explicit FileType(std::string _type) : type(std::move(_type))
		{
		}

		bool operator == (const FileType& _other) const;
		bool operator != (const FileType& _other) const;

	private:
		std::string type;
	};
}
