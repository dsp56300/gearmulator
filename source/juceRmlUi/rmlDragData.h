#pragma once

namespace juceRmlUi
{
	struct DragData
	{
		DragData() = default;
		virtual ~DragData() = default;
	};

	struct FileDragData : DragData
	{
		std::vector<std::string> files;
	};
}
