#pragma once

#include "pluginProcessor.h"
#include "juce_core/juce_core.h"
#include "juce_gui_basics/juce_gui_basics.h"

namespace jucePluginEditorLib
{
	class DragAndDropObject : public juce::ReferenceCountedObject
	{
	public:
		static const DragAndDropObject* fromDragSource(const juce::DragAndDropTarget::SourceDetails& _source)
		{
			return dynamic_cast<const DragAndDropObject*>(_source.description.getObject());
		}

		virtual std::string getExportFileName(const pluginLib::Processor& _processor) const;
		virtual bool writeToFile(const juce::File& _file) const { return false; }
		virtual bool canDropExternally() const { return false; }
	};
}
