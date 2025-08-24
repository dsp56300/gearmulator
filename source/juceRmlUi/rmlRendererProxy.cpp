#include "rmlRendererProxy.h"

#include <cassert>

#include "rmlDataProvider.h"
#include "rmlHelper.h"

#include "baseLib/filesystem.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Log.h"
#include "RmlUi/Core/Variant.h"

namespace juceRmlUi
{
	// ReSharper disable once CppCompileTimeConstantCanBeReplacedWithBooleanConstant
	static_assert(!RendererProxy::InvalidHandle, "expression should return false, is used like that in if expressions");

	RendererProxy::RendererProxy(Rml::CoreInstance& _coreInstance, DataProvider& _dataProvider) : RenderInterface(_coreInstance), m_dataProvider(_dataProvider)
	{
	}

	RendererProxy::~RendererProxy()
	{
		assert(m_handles.empty());
	}

	Rml::CompiledGeometryHandle RendererProxy::CompileGeometry(const Rml::Span<const Rml::Vertex> _vertices, const Rml::Span<const int> _indices)
	{
		auto dummyHandle = createDummyHandle();

		auto vertices = copySpan(_vertices);
		auto indices = copySpan(_indices);

		addRenderFunction(dummyHandle, [this, dummyHandle, v = std::move(vertices), i = std::move(indices)]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->CompileGeometry(v, i);
			addHandle<HandleCompiledGeometry>(dummyHandle, handle);
		});

		return dummyHandle;
	}

	void RendererProxy::RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture)
	{
		addRenderFunction([this, _geometry, _translation, _texture]
		{
			if (const auto realGeometry = getRealHandle<HandleCompiledGeometry>(_geometry))
			{
				if (!_texture)
					m_renderer->RenderGeometry(realGeometry, _translation, _texture);
				else if (const auto realTexture = getRealHandle<HandleTexture>(_texture))
					m_renderer->RenderGeometry(realGeometry, _translation, realTexture);
			}
		});
	}

	void RendererProxy::ReleaseGeometry(Rml::CompiledGeometryHandle _geometry)
	{
		addRenderFunction([this, _geometry]
		{
			if (const auto realGeometry = getRealHandle<HandleCompiledGeometry>(_geometry))
			{
				m_renderer->ReleaseGeometry(realGeometry);
				removeHandle(_geometry);
			}
		});
	}

	Rml::TextureHandle RendererProxy::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		auto dummyHandle = createDummyHandle();

		juce::Image image;

		if (!loadImage(image, _textureDimensions, _source))
			return {};

		addRenderFunction(dummyHandle, [this, dummyHandle, img = std::move(image)]
		{
			if (exists(dummyHandle))
				return;

			std::vector<uint8_t> buffer;

			auto i = img;

			helper::toBuffer(core_instance, buffer, i);

			const auto w = i.getWidth();
			const auto h = i.getHeight();

			const auto handle = m_renderer->GenerateTexture(buffer, Rml::Vector2i(w, h));
			addHandle<HandleTexture>(dummyHandle, handle);
		});

		return dummyHandle;
	}

	Rml::TextureHandle RendererProxy::GenerateTexture(const Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions)
	{
		auto dummyHandle = createDummyHandle();

		auto s = copySpan(_source);

		addRenderFunction(dummyHandle, [this, dummyHandle, source = std::move(s), _sourceDimensions]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->GenerateTexture(source, _sourceDimensions);
			addHandle<HandleTexture>(dummyHandle, handle);
		});

		return dummyHandle;
	}

	void RendererProxy::ReleaseTexture(Rml::TextureHandle _texture)
	{
		addRenderFunction([this, _texture]
		{
			if (const auto realTexture = getRealHandle<HandleTexture>(_texture))
			{
				m_renderer->ReleaseTexture(realTexture);
				removeHandle(_texture);
			}
		});
	}

	void RendererProxy::EnableScissorRegion(bool _enable)
	{
		addRenderFunction([this, _enable]
		{
			m_renderer->EnableScissorRegion(_enable);
		});
	}

	void RendererProxy::SetScissorRegion(Rml::Rectanglei _region)
	{
		addRenderFunction([this, _region]
		{
			m_renderer->SetScissorRegion(_region);
		});
	}

	void RendererProxy::EnableClipMask(const bool _enable)
	{
		addRenderFunction([this, _enable]
		{
			m_renderer->EnableClipMask(_enable);
		});
	}

	void RendererProxy::RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation)
	{
		addRenderFunction([this, _operation, _geometry, _translation]
		{
			if (const auto realGeometry = getRealHandle<HandleCompiledGeometry>(_geometry))
			{
				m_renderer->RenderToClipMask(_operation, realGeometry, _translation);
			}
		});
	}

	void RendererProxy::SetTransform(const Rml::Matrix4f* _transform)
	{
		addRenderFunction([this, matrix = *_transform]
		{
			m_renderer->SetTransform(&matrix);
		});
	}

	Rml::LayerHandle RendererProxy::PushLayer()
	{
		auto dummyHandle = createDummyHandle();

		addRenderFunction(dummyHandle, [this, dummyHandle]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->PushLayer();
			addHandle<HandleLayer>(dummyHandle, handle);
			m_layerHandles.push(dummyHandle);
		});
		return dummyHandle;
	}

	void RendererProxy::CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, const Rml::Span<const Rml::CompiledFilterHandle> _filters)
	{
		auto f = copySpan(_filters);
		addRenderFunction([this, _source, _destination, _blendMode, filters = std::move(f)]
		{
			auto realSource = _source;
			if (realSource)
			{
				realSource = getRealHandle<HandleLayer>(_source);
				if (!realSource)
					return;
			}

			auto realDestination = _destination;
			if (realDestination)
			{
				realDestination = getRealHandle<HandleLayer>(_destination);
				if (!realDestination)
					return;
			}
			
			std::vector<Rml::CompiledFilterHandle> realFilters;
			for (const auto& filter : filters)
			{
				if (const auto realFilter = getRealHandle<HandleCompiledFilter>(filter))
					realFilters.push_back(realFilter);
			}
			m_renderer->CompositeLayers(realSource, realDestination, _blendMode, realFilters);
		});
	}

	void RendererProxy::PopLayer()
	{
		addRenderFunction([this]
		{
			m_renderer->PopLayer();
			const auto dummy = m_layerHandles.top();
			m_layerHandles.pop();
			removeHandle(dummy);
		});
	}

	Rml::TextureHandle RendererProxy::SaveLayerAsTexture()
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction(dummyHandle, [this, dummyHandle]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->SaveLayerAsTexture();
			addHandle<HandleTexture>(dummyHandle, handle);
		});
		return dummyHandle;
	}

	Rml::CompiledFilterHandle RendererProxy::SaveLayerAsMaskImage()
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction(dummyHandle, [this, dummyHandle]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->SaveLayerAsMaskImage();
			addHandle<HandleCompiledFilter>(dummyHandle, handle);
		});
		return dummyHandle;
	}

	Rml::CompiledFilterHandle RendererProxy::CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction(dummyHandle, [this, dummyHandle, name = _name, parameters = _parameters]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->CompileFilter(name, parameters);
			addHandle<HandleCompiledFilter>(dummyHandle, handle);
		});
		return dummyHandle;
	}

	void RendererProxy::ReleaseFilter(Rml::CompiledFilterHandle _filter)
	{
		addRenderFunction([this, _filter]
		{
			if (const auto realFilter = getRealHandle<HandleCompiledFilter>(_filter))
			{
				m_renderer->ReleaseFilter(realFilter);
				removeHandle(_filter);
			}
		});
	}

	Rml::CompiledShaderHandle RendererProxy::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction(dummyHandle, [this, dummyHandle, name = _name, parameters = _parameters]
		{
			if (exists(dummyHandle))
				return;
			auto handle = m_renderer->CompileShader(name, parameters);
			addHandle<HandleCompiledShader>(dummyHandle, handle);
		});
		return dummyHandle;
	}

	void RendererProxy::RenderShader(Rml::CompiledShaderHandle _shader, const Rml::CompiledGeometryHandle _geometry, const Rml::Vector2f _translation, const Rml::TextureHandle _texture)
	{
		addRenderFunction([this, _shader, _geometry, _translation, _texture]
		{
			if (const auto realShader = getRealHandle<HandleCompiledShader>(_shader))
			{
				if (const auto realGeometry = getRealHandle<HandleCompiledGeometry>(_geometry))
				{
					m_renderer->RenderShader(realShader, realGeometry, _translation, _texture);
				}
			}
		});
	}

	void RendererProxy::ReleaseShader(Rml::CompiledShaderHandle _shader)
	{
		addRenderFunction([this, _shader]
		{
			if (const auto realShader = getRealHandle<HandleCompiledShader>(_shader))
			{
				m_renderer->ReleaseShader(realShader);
				removeHandle(_shader);
			}
		});
	}

	void RendererProxy::addRenderFunction(Func _func)
	{
		std::lock_guard lock(m_mutex);
		m_enqueuedFunctions.push_back(std::move(_func));
	}

	void RendererProxy::addRenderFunction(Handle _dummy, const Func& _func)
	{
		std::lock_guard lock(m_mutex);
		m_restoreContextFuncs.insert({ _dummy, _func });
		m_enqueuedFunctions.push_back(std::move(_func));
	}

	bool RendererProxy::executeRenderFunctions()
	{
		bool haveMore = false;

		{
			std::lock_guard lock(m_mutexRender);

			if (!m_renderer)
				return false;

			if (!m_renderFunctions.empty())
			{
				std::swap(m_renderFunctionsToExecute, m_renderFunctions.front());

				if (m_renderFunctions.size() == 1)
					m_renderFunctions.clear();
				else
				{
					m_renderFunctions.erase(m_renderFunctions.begin());
					haveMore = true;
				}
			}
		}

		for (auto& func : m_renderFunctionsToExecute)
			func();

		return haveMore;
	}

	void RendererProxy::finishFrame()
	{
		std::lock_guard lockR(m_mutexRender);
		std::lock_guard lock(m_mutex);
		m_renderFunctions.emplace_back(std::move(m_enqueuedFunctions));
		m_enqueuedFunctions.clear();
	}

	namespace
	{
		void release(Rml::RenderInterface* _r, const RendererProxy::HandleCompiledGeometry& _h) { _r->ReleaseGeometry(_h.handle); }
		void release(Rml::RenderInterface* _r, const RendererProxy::HandleTexture& _h         ) { _r->ReleaseTexture(_h.handle); }
		void release(Rml::RenderInterface* _r, const RendererProxy::HandleCompiledFilter& _h  ) { _r->ReleaseFilter(_h.handle); }
		void release(Rml::RenderInterface* _r, const RendererProxy::HandleCompiledShader& _h  ) { _r->ReleaseShader(_h.handle); }
		void release(Rml::RenderInterface* _r, const RendererProxy::HandleLayer&              ) { _r->PopLayer(); }
	}

	void RendererProxy::setRenderer(Rml::RenderInterface* _renderer)
	{
		std::lock_guard lockR(m_mutexRender);

		if (m_renderer)
		{
			// old renderer might have functions left that need to be executed, for example to release resources
			for (const auto& func : m_renderFunctionsToExecute)
				func();

			{
				std::lock_guard lock(m_mutex);
				if (!m_enqueuedFunctions.empty())
					m_renderFunctions.emplace_back(std::move(m_enqueuedFunctions));
				m_enqueuedFunctions.clear();
			}

			for (const auto& funcs : m_renderFunctions)
			{
				for (const auto& func : funcs)
					func();
			}

			m_renderFunctionsToExecute.clear();
			m_renderFunctions.clear();

			m_enqueuedFunctions.clear();

			// all handles that we have left need to be released by the old renderer
			for (const auto& [dummy,real] : m_handles)
			{
				std::visit([this](auto&& _arg)
		        {
					release(m_renderer, _arg);
		        }, real);
			}

			m_handles.clear();
		}

		m_renderer = _renderer;

		if (m_renderer)
		{
			// restore context for all handles that are still alive
			for (const auto& restoreContextFunc : m_restoreContextFuncs)
				restoreContextFunc.second();
		}
	}

	bool RendererProxy::loadImage(juce::Image& _image, Rml::Vector2i& _textureDimensions, const Rml::String& _source) const
	{
		auto generateDummyImage = [&_image, &_textureDimensions]()
		{
			// Generate a dummy image (1x1 transparent pixel)
			_image = juce::Image(juce::Image::PixelFormat::ARGB, 64, 64, true);

			juce::Graphics g(_image);

			g.fillAll(juce::Colour(0xffff00ff));
			g.setOpacity(1.0f);
			g.setColour(juce::Colours::white);
			g.drawText("Image N/A", 0, 0, _image.getWidth(), _image.getHeight(), juce::Justification::centred, true);

			_textureDimensions.x = _image.getWidth();
			_textureDimensions.y = _image.getHeight();

			return true;
		};

		const char* ptr;
		uint32_t fileSize;

		try
		{
			ptr = m_dataProvider.getResourceByFilename(baseLib::filesystem::getFilenameWithoutPath(_source), fileSize);
		}
		catch (std::runtime_error& e)
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "image file not found: %s", e.what());
			return generateDummyImage();
		}

		if (!ptr)
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "image file not found: %s", _source.c_str());
			return generateDummyImage();
		}

		// Load the texture from the file
		_image = juce::ImageFileFormat::loadFrom(ptr, fileSize);
		if (_image.isNull())
		{
			Rml::Log::Message(core_instance, Rml::Log::LT_ERROR, "Failed to load image from source %s", _source.c_str());
			return generateDummyImage();
		}

		_textureDimensions.x = _image.getWidth();
		_textureDimensions.y = _image.getHeight();

		return true;
	}
}
