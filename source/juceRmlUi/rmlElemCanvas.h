#pragma once

#include "rmlElement.h"

#include "RmlUi/Core/CallbackTexture.h"
#include "RmlUi/Core/Geometry.h"

namespace juce
{
	class Graphics;
}

namespace juce
{
	class Image;
}

namespace juceRmlUi
{
	class ElemCanvas : public Element
	{
	public:
		using RepaintCallback = std::function<void(std::vector<uint8_t>&)>;
		using RepaintGraphicsCallback = std::function<void(juce::Image&, juce::Graphics&)>;

		explicit ElemCanvas(Rml::CoreInstance& _coreInstance, const Rml::String& _tag);

		void setRepaintCallback(const RepaintCallback& _callback) { m_repaintCallback = _callback; }
		void setRepaintGraphicsCallback(const RepaintGraphicsCallback& _callback) { m_repaintGraphicsCallback = _callback; }

		void repaint();

		void setClearEveryFrame(bool _clearEveryFrame);

		static ElemCanvas* create(Rml::Element* _parent);

	private:
		void generateGeometry();
		void generateTexture();

		void OnRender() override;
		void OnResize() override;
		void OnPropertyChange(const Rml::PropertyIdSet& _changedProperties) override;

		void updateImage();

		bool m_geometryDirty = true;
		bool m_textureDirty = true;
		Rml::Geometry m_geometry;
		Rml::Vector2i m_textureSize{ 0, 0 };
		Rml::CallbackTexture m_texture;

		std::unique_ptr<juce::Image> m_image;
		std::vector<uint8_t> m_imageBuffer;
		std::unique_ptr<juce::Graphics> m_graphics;

		RepaintCallback m_repaintCallback;
		RepaintGraphicsCallback m_repaintGraphicsCallback;

		bool m_clearEveryFrame = false;
	};
}
