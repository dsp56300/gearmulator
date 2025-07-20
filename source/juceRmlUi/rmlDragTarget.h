#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rmlDragData.h"

#include "RmlUi/Core/EventListener.h"

namespace juceRmlUi
{
	class DragSource;
}

namespace Rml
{
	class Event;
	class Element;
}

namespace juceRmlUi
{
	class DragTarget
	{
	public:
		class KeyEventListener : public Rml::EventListener
		{
		public:
			explicit KeyEventListener(DragTarget& _target);
			~KeyEventListener() override;

			void ProcessEvent(Rml::Event& _event) override;

		private:
			DragTarget& m_target;
			Rml::Element* m_rootElement = nullptr;
		};

		enum class DragLocation : uint8_t
		{
			None = 0,

			Left = 1,
			Center = 2,
			Right = 3,

			Top = Left,
			Bottom = Right,
		};

		DragTarget(Rml::Element* _elem = nullptr);
		~DragTarget();

		bool init(Rml::Element* _elem);

		virtual bool canDrop(const Rml::Event& _event, const DragSource* _source) const;
		virtual bool canDropFiles(const Rml::Event& _event, const std::vector<std::string>& _files) const;

		void setAllowLocations(const bool _horizontal, const bool _vertical)
		{
			m_allowLocationHorizontal = _horizontal;
			m_allowLocationVertical = _vertical;
		}

		void setAllowShift(bool _shift)
		{
			m_allowShift = _shift;
		}

		static DragTarget* fromElement(const Rml::Element* _elem);

		virtual void drop(const Rml::Event& _event, const DragData* _data) {}
		virtual void dropFiles(const Rml::Event& _event, const FileDragData* _data, const std::vector<std::string>& _files) {}

	private:
		void onDragOver(const Rml::Event& _event);
		void onDragMove(const Rml::Event& _event);
		void onDragOut(const Rml::Event& _event);
		void onDragDrop(const Rml::Event& _event);
		void onKeyChange(const Rml::Event& _event);

		void stopDrag();

		void updateDragLocation(const Rml::Event& _event);

		void updatePseudoClass(DragLocation _locationH, DragLocation _locationV);
		void updatePseudoClass(const std::string& _pseudoClass);
		void updateShiftPseudoClass(bool _shift);

		Rml::Element* m_element = nullptr;
		DragSource* m_currentDragSource = nullptr;

		bool m_allowLocationVertical = true;
		bool m_allowLocationHorizontal = true;
		bool m_allowShift = true;

		bool m_shiftDown = false;

		DragLocation m_currentLocationH = DragLocation::None;
		DragLocation m_currentLocationV = DragLocation::None;

		std::string m_currentLocationPseudoClass;

		std::unique_ptr<KeyEventListener> m_keyEventListener;
	};
}
