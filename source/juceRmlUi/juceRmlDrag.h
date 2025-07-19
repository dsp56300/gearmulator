#pragma once

#include "rmlDragSource.h"

#include "juce_gui_basics/juce_gui_basics.h"

#include "RmlUi/Core/Element.h"
#include "RmlUi/Core/EventListener.h"

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
	class RmlDrag : Rml::EventListener
	{
	public:
		struct JuceDraggable : juce::ReferenceCountedObject
		{
			DragData* data = nullptr;
		};

		class FileDragSource : public DragSource
		{
		public:
			FileDragSource(Rml::Element* _element, const juce::StringArray& _files);
			std::unique_ptr<DragData> createDragData() override;

		private:
			std::vector<std::string> m_files;
		};

		explicit RmlDrag(RmlComponent& _component);
		~RmlDrag() override;

		void onDocumentLoaded();

		bool isInterestedInFileDrag(const juce::StringArray& _files);
		void fileDragEnter(const juce::StringArray& _files, int _x, int _y);
		void fileDragMove(const juce::StringArray& _files, int _x, int _y) const;
		void fileDragExit(const juce::StringArray& _files);
		void filesDropped(const juce::StringArray& _files, int _x, int _y);
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDragExit(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& _sourceDetails, juce::StringArray& _files, bool& _canMoveFiles) const;

		void processOutOfBoundsDrag(juce::Point<int> _pos);

	private:
		void startDrag(int _x, int _y, const juce::StringArray& _files);
		void stopDrag();

		void ProcessEvent(Rml::Event& _event) override;

		RmlComponent& m_component;
		Rml::Element* m_draggable = nullptr;
		std::unique_ptr<DragSource> m_juceDragSource;
		DragSource* m_rmlDragSource = nullptr;
		JuceDraggable* m_juceDraggable = nullptr;
	};
}
