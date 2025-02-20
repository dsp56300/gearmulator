#pragma once

#include <string>

namespace jucePluginEditorLib
{
	class FileType
	{
	public:
		static const FileType Syx;
		static const FileType Mid;

		explicit FileType(std::string _type) : m_type(std::move(_type))
		{
		}

		bool operator == (const FileType& _other) const;
		bool operator != (const FileType& _other) const;

	private:
		std::string m_type;
	};
}
