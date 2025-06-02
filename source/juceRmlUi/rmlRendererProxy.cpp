#include "rmlRendererProxy.h"

#include "juceRmlComponent.h"
#include "rmlRenderInterface.h"
#include "baseLib/filesystem.h"

#include "juce_graphics/juce_graphics.h"

namespace juceRmlUi
{
	// ReSharper disable once CppCompileTimeConstantCanBeReplacedWithBooleanConstant
	static_assert(!RendererProxy::InvalidHandle, "expression should return false, is used like that in ifs");

	RendererProxy::RendererProxy(juceRmlUi::RenderInterface& _renderer): m_renderer(_renderer)
	{
	}

	RendererProxy::~RendererProxy()	= default;

	Rml::CompiledGeometryHandle RendererProxy::CompileGeometry(const Rml::Span<const Rml::Vertex> _vertices, const Rml::Span<const int> _indices)
	{
		auto dummyHandle = createDummyHandle();

		auto vertices = copySpan(_vertices);
		auto indices = copySpan(_indices);

		addRenderFunction([this, dummyHandle, v = std::move(vertices), i = std::move(indices)]
		{
			auto handle = m_renderer.CompileGeometry(v, i);
			addHandle(dummyHandle, handle);
		});

		return dummyHandle;
	}

	void RendererProxy::RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture)
	{
		addRenderFunction([this, _geometry, _translation, _texture]
		{
			if (const auto realGeometry = getRealHandle(_geometry))
			{
				if (!_texture)
					m_renderer.RenderGeometry(realGeometry, _translation, _texture);
				else if (const auto realTexture = getRealHandle(_texture))
					m_renderer.RenderGeometry(realGeometry, _translation, realTexture);
			}
		});
	}

	void RendererProxy::ReleaseGeometry(Rml::CompiledGeometryHandle _geometry)
	{
		addRenderFunction([this, _geometry]
		{
			if (const auto realGeometry = getRealHandle(_geometry))
			{
				m_renderer.ReleaseGeometry(realGeometry);
				removeHandle(_geometry);
			}
		});
	}

	Rml::TextureHandle RendererProxy::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		juce::Image image;
		if (!m_renderer.loadImage(image, _textureDimensions, _source))
			return {};

		auto dummyHandle = createDummyHandle();

		addRenderFunction([this, dummyHandle, image]
		{
			auto img = image;
			const auto handle = juceRmlUi::RenderInterface::loadTexture(img);
			addHandle(dummyHandle, handle);
		});

		return dummyHandle;
	}

	Rml::TextureHandle RendererProxy::GenerateTexture(const Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions)
	{
		auto dummyHandle = createDummyHandle();

		auto s = copySpan(_source);

		addRenderFunction([this, dummyHandle, source = std::move(s), _sourceDimensions]
		{
			auto handle = m_renderer.GenerateTexture(source, _sourceDimensions);
			addHandle(dummyHandle, handle);
		});

		return dummyHandle;
	}

	void RendererProxy::ReleaseTexture(Rml::TextureHandle _texture)
	{
		addRenderFunction([this, _texture]
		{
			if (const auto realTexture = getRealHandle(_texture))
			{
				m_renderer.ReleaseTexture(realTexture);
				removeHandle(_texture);
			}
		});
	}

	void RendererProxy::EnableScissorRegion(bool _enable)
	{
		addRenderFunction([this, _enable]
		{
			m_renderer.EnableScissorRegion(_enable);
		});
	}

	void RendererProxy::SetScissorRegion(Rml::Rectanglei _region)
	{
		addRenderFunction([this, _region]
		{
			m_renderer.SetScissorRegion(_region);
		});
	}

	void RendererProxy::EnableClipMask(const bool _enable)
	{
		addRenderFunction([this, _enable]
		{
			RenderInterface::EnableClipMask(_enable);
		});
	}

	void RendererProxy::RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation)
	{
		addRenderFunction([this, _operation, _geometry, _translation]
		{
			if (const auto realGeometry = getRealHandle(_geometry))
			{
				RenderInterface::RenderToClipMask(_operation, realGeometry, _translation);
			}
		});
	}

	void RendererProxy::SetTransform(const Rml::Matrix4f* _transform)
	{
		addRenderFunction([this, matrix = *_transform]
		{
			RenderInterface::SetTransform(&matrix);
		});
	}

	Rml::LayerHandle RendererProxy::PushLayer()
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle]
		{
			auto handle = m_renderer.PushLayer();
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	void RendererProxy::CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, const Rml::Span<const Rml::CompiledFilterHandle> _filters)
	{
		auto f = copySpan(_filters);
		addRenderFunction([this, _source, _destination, _blendMode, filters = std::move(f)]
		{
			auto realSource = getRealHandle(_source);
			auto realDestination = getRealHandle(_destination);
			if (realSource/* && realDestination*/)	// destination can be invalid, then it's the main back buffer
			{
				std::vector<Rml::CompiledFilterHandle> realFilters;
				for (const auto& filter : filters)
				{
					if (const auto realFilter = getRealHandle(filter))
						realFilters.push_back(realFilter);
				}
				m_renderer.CompositeLayers(realSource, realDestination, _blendMode, realFilters);
			}
		});
	}

	void RendererProxy::PopLayer()
	{
		addRenderFunction([this]
		{
			m_renderer.PopLayer();
		});
	}

	Rml::TextureHandle RendererProxy::SaveLayerAsTexture()
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle]
		{
			auto handle = m_renderer.SaveLayerAsTexture();
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	Rml::CompiledFilterHandle RendererProxy::SaveLayerAsMaskImage()
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle]
		{
			auto handle = m_renderer.SaveLayerAsMaskImage();
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	Rml::CompiledFilterHandle RendererProxy::CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle, name = _name, parameters = _parameters]
		{
			auto handle = m_renderer.CompileFilter(name, parameters);
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	void RendererProxy::ReleaseFilter(Rml::CompiledFilterHandle _filter)
	{
		addRenderFunction([this, _filter]
		{
			if (const auto realFilter = getRealHandle(_filter))
			{
				RenderInterface::ReleaseFilter(realFilter);
				removeHandle(_filter);
			}
		});
	}

	Rml::CompiledShaderHandle RendererProxy::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle, name = _name, parameters = _parameters]
		{
			auto handle = m_renderer.CompileShader(name, parameters);
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	void RendererProxy::RenderShader(Rml::CompiledShaderHandle _shader, const Rml::CompiledGeometryHandle _geometry, const Rml::Vector2f _translation, const Rml::TextureHandle _texture)
	{
		addRenderFunction([this, _shader, _geometry, _translation, _texture]
		{
			if (const auto realShader = getRealHandle(_shader))
			{
				if (const auto realGeometry = getRealHandle(_geometry))
				{
					m_renderer.RenderShader(realShader, realGeometry, _translation, _texture);
				}
			}
		});
	}

	void RendererProxy::ReleaseShader(Rml::CompiledShaderHandle _shader)
	{
		addRenderFunction([this, _shader]
		{
			if (const auto realShader = getRealHandle(_shader))
			{
				RenderInterface::ReleaseShader(realShader);
				removeHandle(_shader);
			}
		});
	}

	void RendererProxy::addRenderFunction(Func _func)
	{
		std::lock_guard lock(m_mutex);
		m_renderFunctions.push_back(std::move(_func));
	}

	void RendererProxy::executeEnqueuedFunctions()
	{
		std::vector<Func> renderFunctions;
		{
			std::lock_guard lock(m_mutex);
			renderFunctions.swap(m_renderFunctions);
		}

		for (auto& func : renderFunctions)
			func();
	}
}
