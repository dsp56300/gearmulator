#include "dragAndDropObject.h"

namespace jucePluginEditorLib
{
	std::string DragAndDropObject::getExportFileName(const pluginLib::Processor& _processor) const
	{
		return _processor.getProperties().name + "_";
	}
}
