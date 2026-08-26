#pragma once

#include <string>
#include <cstdint>

namespace pluginLib
{
	class FileType
	{
	public:
		static const FileType Syx;
		static const FileType Mid;
		static const FileType Empty;

		explicit FileType(std::string _type) : m_type(std::move(_type))
		{
		}

		bool operator == (const FileType& _other) const;
		bool operator != (const FileType& _other) const;

		const std::string& type() const { return m_type; }

	private:
		std::string m_type;
	};

	enum class ExportType : uint8_t
	{
		Clipboard,
		DragAndDrop,
		EmuHardware,
		File
	};
}
