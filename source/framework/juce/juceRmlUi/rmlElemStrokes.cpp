#include "rmlElemStrokes.h"

#include <cmath>

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/Mesh.h"
#include "RmlUi/Core/Property.h"
#include "RmlUi/Core/RenderManager.h"

namespace juceRmlUi
{
	namespace
	{
		// pixels of the soft edge on both sides, it smooths the line where the renderer does not
		constexpr float g_feather = 1.0f;

		// One strip along the points, _width wide without the soft edges. Four vertices per point: the soft edge, the
		// line, the line, the soft edge
		void addStrip(Rml::Mesh& _mesh, const ElemStrokes::Points& _points, const float _scale, const float _width, const Rml::Colourb _colour, const float _opacity)
		{
			if(_points.size() < 2 || _width <= 0.0f)
				return;

			const auto solid = _colour.ToPremultiplied(_opacity);
			const auto clear = _colour.ToPremultiplied(0.0f);
			const auto half = _width * 0.5f;

			const auto base = static_cast<int>(_mesh.vertices.size());

			for(size_t i=0; i<_points.size(); ++i)
			{
				// the direction at a point: the average of the segments that meet there
				const auto& prev = _points[i ? i - 1 : 0];
				const auto& next = _points[i + 1 < _points.size() ? i + 1 : i];
				auto d = (next - prev) * _scale;
				const auto len = std::sqrt(d.x * d.x + d.y * d.y);
				d = len > 0.0f ? d / len : Rml::Vector2f(1.0f, 0.0f);
				const Rml::Vector2f n(-d.y, d.x);

				const auto p = _points[i] * _scale;

				_mesh.vertices.push_back({p + n * (half + g_feather), clear, {}});
				_mesh.vertices.push_back({p + n * half, solid, {}});
				_mesh.vertices.push_back({p - n * half, solid, {}});
				_mesh.vertices.push_back({p - n * (half + g_feather), clear, {}});
			}

			for(size_t i=0; i+1<_points.size(); ++i)
			{
				const auto a = base + static_cast<int>(i) * 4;
				const auto b = a + 4;

				// the soft edges, the line, each quad as two triangles. A soft edge starts with its clear vertex: a renderer
				// that takes the colour of the first vertex for the whole triangle leaves them out
				const int quads[3][4] = {{a, a + 1, b + 1, b}, {a + 1, a + 2, b + 2, b + 1}, {a + 3, a + 2, b + 2, b + 3}};
				for (const auto& q : quads)
					_mesh.indices.insert(_mesh.indices.end(), {q[0], q[1], q[2], q[0], q[2], q[3]});
			}
		}
	}

	ElemStrokes::ElemStrokes(Rml::CoreInstance& _coreInstance, const Rml::String& _tag) : Element(_coreInstance, _tag)
	{
	}

	void ElemStrokes::setStrokes(std::vector<Points> _strokes)
	{
		m_strokes = std::move(_strokes);
		m_dirty = true;
		if(auto* context = GetContext())
			context->RequestNextUpdate(0);
	}

	void ElemStrokes::OnRender()
	{
		Element::OnRender();

		if(m_dirty)
			generateGeometry();

		m_geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Border));
	}

	void ElemStrokes::OnPropertyChange(const Rml::PropertyIdSet& _changedProperties)
	{
		Element::OnPropertyChange(_changedProperties);
		m_dirty = true;
	}

	void ElemStrokes::OnDpRatioChange()
	{
		Element::OnDpRatioChange();
		m_dirty = true;
	}

	void ElemStrokes::generateGeometry()
	{
		m_dirty = false;

		auto mesh = m_geometry.Release(Rml::Geometry::ReleaseMode::ClearMesh);

		auto* context = GetContext();
		auto* rm = GetRenderManager();
		if(!context || !rm)
			return;

		const auto scale = context->GetDensityIndependentPixelRatio();

		auto length = [this](const char* _property)
		{
			const auto* p = GetProperty(_property);
			return p ? ResolveLength(p->GetNumericValue(GetCoreInstance())) : 0.0f;
		};

		const auto width = length("stroke-width");
		const auto outline = length("stroke-outline-width");

		const auto& computed = GetComputedValues();
		const auto colour = computed.color();
		const auto opacity = computed.opacity();

		const auto* outlineProperty = GetProperty("stroke-outline-color");
		const auto outlineColour = outlineProperty ? outlineProperty->Get<Rml::Colourb>(GetCoreInstance()) : Rml::Colourb(0, 0, 0, 255);

		// each line over its own outline, a later line crosses an earlier one
		for (const auto& points : m_strokes)
		{
			if(outline > 0.0f)
				addStrip(mesh, points, scale, width + 2.0f * outline, outlineColour, opacity);
			addStrip(mesh, points, scale, width, colour, opacity);
		}

		if(!mesh.indices.empty())
			m_geometry = rm->MakeGeometry(std::move(mesh));
	}
}
