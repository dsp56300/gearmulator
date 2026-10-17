#pragma once

#include <vector>

#include "rmlElement.h"

#include "RmlUi/Core/Geometry.h"
#include "RmlUi/Core/Texture.h"

namespace juceRmlUi
{
	// Draws lines through points, curves sampled into short segments for example, as one mesh. The line takes the
	// colour of the property color and the width of stroke-width, an outline around it stroke-outline-width and
	// stroke-outline-color. stroke-image, an image file, is laid across the lines and their outlines times their colour:
	// its top along one side, its bottom along the other, a cable is shaded round with a gradient that is dark at both.
	// The points are in dp, relative to the element's top left
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
		Rml::Texture m_texture;
		Rml::String m_textureSource;
		bool m_dirty = true;
	};
}
