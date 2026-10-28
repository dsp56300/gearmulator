#pragma once

#include <memory>

#include "rmlEventListener.h"

namespace Rml
{
	class Event;
	class Element;
}

namespace juceRmlUi
{
	struct DragData;

	class DragSource
	{
	public:
		DragSource(Rml::Element* _element = nullptr);
		DragSource(const DragSource&) = delete;
		DragSource(DragSource&&) = delete;
		virtual ~DragSource();

		DragSource& operator=(const DragSource&) = delete;
		DragSource& operator=(DragSource&&) = delete;

		bool init(Rml::Element* _element);

		virtual std::unique_ptr<DragData> createDragData() = 0;

		DragData* getDragData() const noexcept { return m_dragData.get(); }

		// Whether a drag target took what this source offered. Set by the target that accepted the
		// drop, and asked afterwards by whoever started the drag - a file drag that no element
		// wanted is one the window itself may still want. Lives on the source and not on the data
		// because the data is gone by the time the drop returns: dragend releases it.
		void setHandled() noexcept { m_handled = true; }
		bool isHandled() const noexcept { return m_handled; }

		// Escape during a drag cancels it: nothing is dropped and the drag ends as if released. Targets
		// refuse a cancelled drag (DragTarget).
		bool isCancelled() const noexcept { return m_cancelled; }

		static DragSource* fromElement(const Rml::Element* _elem);

		Rml::Element* getElement() const noexcept { return m_element; }

	private:
		void onDragStart(const Rml::Event& _event);
		void onDragEnd(const Rml::Event& _event);
		void onKeyDown(Rml::Event& _event);
		void endCancelledDrag();

		std::unique_ptr<DragData> m_dragData;
		bool m_handled = false;
		bool m_cancelled = false;
		ScopedListener m_escapeListener;

		Rml::Element* m_element = nullptr;
	};
}
