#pragma once

#include <cstdint>
#include <string>

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
		enum class DragLocation : uint8_t
		{
			None,

			Left,
			Center,
			Right,

			Top = Left,
			Bottom = Right,
		};

		DragTarget(Rml::Element* _elem = nullptr);
		~DragTarget();

		bool init(Rml::Element* _elem);

		virtual bool canDrop(const DragSource* _source) const { return _source != nullptr; }

		void setAllowLocations(const bool _horizontal, const bool _vertical)
		{
			m_allowLocationHorizontal = _horizontal;
			m_allowLocationVertical = _vertical;
		}

		static DragTarget* fromElement(const Rml::Element* _elem);

	private:
		void onDragOver(const Rml::Event& _event);
		void onDragMove(const Rml::Event& _event);
		void onDragOut(const Rml::Event& _event);
		void onDragDrop(const Rml::Event& _event);

		void updateDragLocation(const Rml::Event& _event);

		void updatePseudoClass(DragLocation _locationH, DragLocation _locationV);
		void updatePseudoClass(const std::string& _pseudoClass);

		Rml::Element* m_element = nullptr;
		DragSource* m_currentDragSource = nullptr;

		bool m_allowLocationVertical = true;
		bool m_allowLocationHorizontal = true;

		DragLocation m_currentLocationH = DragLocation::None;
		DragLocation m_currentLocationV = DragLocation::None;

		std::string m_currentLocationPseudoClass;
	};
}
