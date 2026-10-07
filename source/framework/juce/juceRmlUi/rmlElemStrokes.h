#pragma once

#include <vector>

#include "rmlElement.h"

#include "RmlUi/Core/Geometry.h"

namespace juceRmlUi
{
	// Draws lines through points, curves sampled into short segments for example, as one mesh. The line takes the
	// colour of the property color and the width of stroke-width, an outline around it stroke-outline-width and
	// stroke-outline-color. The points are in dp, relative to the element's top left
	class ElemStrokes : public Element
	{
	public:
		using Points = std::vector<Rml::Vector2f>;

		explicit ElemStrokes(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);

		void setStrokes(std::vector<Points> _strokes);

	private:
		void OnRender() override;
		void OnPropertyChange(const Rml::PropertyIdSet& _changedProperties) override;
		void OnDpRatioChange() override;

		void generateGeometry();

		std::vector<Points> m_strokes;
		Rml::Geometry m_geometry;
		bool m_dirty = true;
	};
}
