#pragma once

#include "rmlDragSource.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "RmlUi/Core/Element.h"

namespace juce
{
	class StringArray;
}

namespace juceRmlUi
{
	class RmlComponent;
}

namespace juceRmlUi
{
	class RmlDrag
	{
	public:
		class FileDragSource : public DragSource
		{
		public:
			FileDragSource(Rml::Element* _element, const juce::StringArray& _files);
			std::unique_ptr<DragData> createDragData() override;

		private:
			std::vector<std::string> m_files;
		};

		RmlDrag(RmlComponent& _component);
		~RmlDrag();

		bool isInterestedInFileDrag(const juce::StringArray& _files);
		void fileDragEnter(const juce::StringArray& _files, int _x, int _y);
		void fileDragMove(const juce::StringArray& _files, int _x, int _y) const;
		void fileDragExit(const juce::StringArray& _files);
		void filesDropped(const juce::StringArray& _files, int _x, int _y);
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDragExit(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);

	private:
		void startDrag(int _x, int _y, const juce::StringArray& _files);
		void stopDrag();

		RmlComponent& m_component;
		Rml::Element* m_draggable = nullptr;
		std::unique_ptr<DragSource> m_dragSource;
	};
}
