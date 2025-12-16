#include "rmlElemCanvas.h"

#include "juceRmlComponent.h"
#include "rmlHelper.h"
#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/ComputedValues.h"
#include "RmlUi/Core/Context.h"
#include "RmlUi/Core/ElementDocument.h"
#include "RmlUi/Core/Geometry.h"
#include "RmlUi/Core/Mesh.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi/Core/PropertyIdSet.h"
#include "RmlUi/Core/RenderManager.h"

namespace juceRmlUi
{
	using namespace Rml;

	ElemCanvas::ElemCanvas(Rml::CoreInstance& _coreInstance, const String& _tag): Element(_coreInstance, _tag)
	{
		m_repaintCallback = [this](std::vector<uint8_t>& _buffer)
		{
			updateImage();
			helper::toBuffer(GetCoreInstance(), _buffer, *m_image);
		};

		m_repaintGraphicsCallback = [this](const juce::Image& _image, juce::Graphics& _g)
		{
			_g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff0000), 0, 0, juce::Colour(0xff0000ff), static_cast<float>(_image.getWidth()), static_cast<float>(_image.getHeight()), false));
			_g.fillAll();
			_g.setColour(juce::Colour(0xffffffff));
			_g.drawFittedText("Set Repaint Graphics Callback", 0, 0, _image.getWidth(), _image.getHeight(), juce::Justification::centred, 1);
		};
	}

	void ElemCanvas::repaint()
	{
		m_textureDirty = true;
		auto* comp = RmlComponent::fromElement(this);
		if (comp)
			comp->enqueueUpdate();
		else
			GetContext()->RequestNextUpdate(0);
	}

	void ElemCanvas::setClearEveryFrame(const bool _clearEveryFrame)
	{
		m_clearEveryFrame = _clearEveryFrame;
	}

	ElemCanvas* ElemCanvas::create(Rml::Element* _parent)
	{
		auto canvas = _parent->GetOwnerDocument()->CreateElement("canvas");

		auto c = dynamic_cast<ElemCanvas*>(_parent->AppendChild(std::move(canvas)));

		c->SetProperty(PropertyId::Position, Style::Position::Absolute);
		c->SetProperty(PropertyId::Left, Property(0, Unit::PX));
		c->SetProperty(PropertyId::Top, Property(0, Unit::PX));
		c->SetProperty(PropertyId::Width, Property(100, Unit::PERCENT));
		c->SetProperty(PropertyId::Height, Property(100, Unit::PERCENT));

		return c;
	}

	void ElemCanvas::generateGeometry()
	{
		// Release the old geometry before specifying the new vertices
		auto mesh = m_geometry.Release(Geometry::ReleaseMode::ClearMesh);

		const auto& computed = GetComputedValues();
		const auto quadColour = computed.image_color().ToPremultiplied(computed.opacity());
		const auto renderBox = GetRenderBox(BoxArea::Content);

		const auto origin = renderBox.GetFillOffset();
		const auto size = renderBox.GetFillSize();

		const auto* comp = RmlComponent::fromElement(this);

		const auto w = static_cast<int>(comp->getValidTextureSize(static_cast<uint32_t>(size.x)));
		const auto h = static_cast<int>(comp->getValidTextureSize(static_cast<uint32_t>(size.y)));

		if (w != m_textureSize.x || h != m_textureSize.y)
		{
			m_textureSize.x = w;
			m_textureSize.y = h;

			m_textureDirty = true;
		}

		MeshUtilities::GenerateQuad(mesh, origin, size, quadColour, Vector2f(0,0), Vector2f(1,1));

		if (RenderManager* rm = GetRenderManager())
			m_geometry = rm->MakeGeometry(std::move(mesh));

		m_geometryDirty = false;
	}

	void ElemCanvas::generateTexture()
	{
		if (RenderManager* rm = GetRenderManager())
		{
			if (m_texture)
				m_texture.Release();

			if (m_textureSize.x > 0 && m_textureSize.y > 0)
			{
				m_texture = rm->MakeCallbackTexture([this](const CallbackTextureInterface& _callbackTextureInterface) -> bool
				{
					m_imageBuffer.resize(m_textureSize.x * m_textureSize.y * 4); // RGBA format
					m_repaintCallback(m_imageBuffer);
					_callbackTextureInterface.GenerateTexture(Span<const byte>(m_imageBuffer), m_textureSize);
					return true;
				});
			}
		}
		m_textureDirty = false;
	}

	void ElemCanvas::OnRender()
	{
		Element::OnRender();

		if (m_geometryDirty)
			generateGeometry();
		if (m_textureDirty)
			generateTexture();

		if (m_textureSize.x > 0 && m_textureSize.y > 0)
			m_geometry.Render(GetAbsoluteOffset(BoxArea::Border), m_texture);
	}

	void ElemCanvas::OnResize()
	{
		Element::OnResize();
		m_geometryDirty = true;
	}

	void ElemCanvas::OnPropertyChange(const Rml::PropertyIdSet& _changedProperties)
	{
		Element::OnPropertyChange(_changedProperties);

		if (_changedProperties.Contains(PropertyId::ImageColor) || _changedProperties.Contains(PropertyId::Opacity))
			m_geometryDirty = true;
	}

	void ElemCanvas::updateImage()
	{
		if (!m_image || m_image->getWidth() != m_textureSize.x || m_image->getHeight() != m_textureSize.y)
		{
			m_image.reset(new juce::Image(juce::Image::ARGB, m_textureSize.x, m_textureSize.y, false));
			m_graphics.reset(new juce::Graphics(*m_image));
		}

		if (m_clearEveryFrame)
		{
			juce::Image::BitmapData bitmapData(*m_image, juce::Image::BitmapData::writeOnly);
			memset(bitmapData.data, 0, bitmapData.size);
		}

		m_repaintGraphicsCallback(*m_image, *m_graphics);
	}
}
