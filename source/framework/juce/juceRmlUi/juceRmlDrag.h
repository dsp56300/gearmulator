#pragma once

#include <functional>
#include <string>
#include <vector>

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
	struct JuceDraggable;
	class RmlComponent;
}

namespace juceRmlUi
{
	class RmlDrag : Rml::EventListener
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

		explicit RmlDrag(RmlComponent& _component);
		~RmlDrag() override;

		void onDocumentLoaded();

		// Files dropped on the component that no element in the document took. Without this they
		// are dropped on the floor, because the component says yes to every file drag so that an
		// element may have it - which also means JUCE never offers the drop to anything above.
		using UnhandledFilesCallback = std::function<void(const std::vector<std::string>&)>;

		void setUnhandledFilesCallback(UnhandledFilesCallback _callback) { m_onUnhandledFiles = std::move(_callback); }

		// Whether an element took the files of the last drop, for tools that drop files themselves
		bool wasLastDropHandled() const noexcept { return m_lastDropHandled; }

		bool isInterestedInFileDrag(const juce::StringArray& _files);
		void fileDragEnter(const juce::StringArray& _files, int _x, int _y);
		void fileDragMove(const juce::StringArray& _files, int _x, int _y) const;
		void fileDragExit(const juce::StringArray& _files);
		void filesDropped(const juce::StringArray& _files, int _x, int _y);
		bool isInterestedInDragSource(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDragEnter(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDragExit(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		void itemDropped(const juce::DragAndDropTarget::SourceDetails& _dragSourceDetails);
		bool shouldDropFilesWhenDraggedExternally(const juce::DragAndDropTarget::SourceDetails& _sourceDetails, juce::StringArray& _files, bool& _canMoveFiles);

		void processOutOfBoundsDrag(juce::Point<int> _pos);
		// true if it released the mouse button the context still considered held
		bool dragOperationEnded();

	private:
		void startDrag(int _x, int _y, const juce::StringArray& _files);
		void stopDrag();

		void ProcessEvent(Rml::Event& _event) override;

		RmlComponent& m_component;
		Rml::Element* m_draggable = nullptr;
		std::unique_ptr<DragSource> m_juceDragSource;
		DragSource* m_rmlDragSource = nullptr;
		JuceDraggable* m_juceDraggable = nullptr;
		UnhandledFilesCallback m_onUnhandledFiles;
		bool m_lastDropHandled = false;
		bool m_externalDragStarted = false;
	};
}
