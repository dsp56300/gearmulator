#pragma once

#include <memory>

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

		static DragSource* fromElement(const Rml::Element* _elem);

		Rml::Element* getElement() const noexcept { return m_element; }

	private:
		void onDragStart(const Rml::Event& _event);
		void onDragEnd(const Rml::Event& _event);

		std::unique_ptr<DragData> m_dragData;

		Rml::Element* m_element = nullptr;
	};
}
