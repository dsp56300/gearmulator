#pragma once

namespace juceRmlUi
{
	struct DragData
	{
		DragData() = default;
		virtual ~DragData() = default;

		virtual bool canExportAsFiles() const { return false; }

		virtual bool getFilesForExport(std::vector<std::string>& _files, bool& _filesAreTemporary)
		{
			return false; // default implementation does not support file export
		}
	};

	struct FileDragData : DragData
	{
		std::vector<std::string> files;
	};
}
