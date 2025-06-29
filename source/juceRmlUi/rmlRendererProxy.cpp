#include "rmlRendererProxy.h"

#include <cassert>

#include "rmlDataProvider.h"

#include "baseLib/filesystem.h"

#include "juce_graphics/juce_graphics.h"

#include "RmlUi/Core/Log.h"
#include "RmlUi/Core/Variant.h"

namespace juceRmlUi
{
	// ReSharper disable once CppCompileTimeConstantCanBeReplacedWithBooleanConstant
	static_assert(!RendererProxy::InvalidHandle, "expression should return false, is used like that in if expressions");

	RendererProxy::RendererProxy(DataProvider& _dataProvider) : m_dataProvider(_dataProvider)
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
			auto handle = m_renderer->CompileGeometry(v, i);
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
					m_renderer->RenderGeometry(realGeometry, _translation, _texture);
				else if (const auto realTexture = getRealHandle(_texture))
					m_renderer->RenderGeometry(realGeometry, _translation, realTexture);
			}
		});
	}

	void RendererProxy::ReleaseGeometry(Rml::CompiledGeometryHandle _geometry)
	{
		addRenderFunction([this, _geometry]
		{
			if (const auto realGeometry = getRealHandle(_geometry))
			{
				m_renderer->ReleaseGeometry(realGeometry);
				removeHandle(_geometry);
			}
		});
	}

	Rml::TextureHandle RendererProxy::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		juce::Image image;
		if (!loadImage(image, _textureDimensions, _source))
			return {};

		return loadTexture(image);
	}

	Rml::TextureHandle RendererProxy::GenerateTexture(const Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions)
	{
		auto dummyHandle = createDummyHandle();

		auto s = copySpan(_source);

		addRenderFunction([this, dummyHandle, source = std::move(s), _sourceDimensions]
		{
			auto handle = m_renderer->GenerateTexture(source, _sourceDimensions);
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
			if (const auto realGeometry = getRealHandle(_geometry))
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

		addRenderFunction([this, dummyHandle]
		{
			auto handle = m_renderer->PushLayer();
			addHandle(dummyHandle, handle);
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
				realSource = getRealHandle(_source);
				if (!realSource)
					return;
			}

			auto realDestination = _destination;
			if (realDestination)
			{
				realDestination = getRealHandle(_destination);
				if (!realDestination)
					return;
			}
			
			std::vector<Rml::CompiledFilterHandle> realFilters;
			for (const auto& filter : filters)
			{
				if (const auto realFilter = getRealHandle(filter))
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
		addRenderFunction([this, dummyHandle]
		{
			auto handle = m_renderer->SaveLayerAsTexture();
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	Rml::CompiledFilterHandle RendererProxy::SaveLayerAsMaskImage()
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle]
		{
			auto handle = m_renderer->SaveLayerAsMaskImage();
			addHandle(dummyHandle, handle);
		});
		return dummyHandle;
	}

	Rml::CompiledFilterHandle RendererProxy::CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle, name = _name, parameters = _parameters]
		{
			auto handle = m_renderer->CompileFilter(name, parameters);
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
				m_renderer->ReleaseFilter(realFilter);
				removeHandle(_filter);
			}
		});
	}

	Rml::CompiledShaderHandle RendererProxy::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto dummyHandle = createDummyHandle();
		addRenderFunction([this, dummyHandle, name = _name, parameters = _parameters]
		{
			auto handle = m_renderer->CompileShader(name, parameters);
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
					m_renderer->RenderShader(realShader, realGeometry, _translation, _texture);
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

	Rml::TextureHandle RendererProxy::loadTexture(juce::Image& _image)
	{
		const auto w = _image.getWidth();
		const auto h = _image.getHeight();
		const auto pixelCount = w * h;

		std::vector<uint8_t> buffer;

		juce::Image::BitmapData bitmapData(_image, 0, 0, w, h, juce::Image::BitmapData::readOnly);

		uint32_t bufferIndex = 0;

		switch(_image.getFormat())
		{
		case juce::Image::ARGB:
			buffer.resize(pixelCount * 4);
			for (int y=0; y<h; ++y)
			{
				for (int x=0; x<w; ++x)
				{
					// juce rturns an unpremultiplied color but we want a premultiplied pixel value
					// the function getPixelColour casts the pixel pointer to PixelARGB* but this
					// might change. Verify that its still the case and modify this assert accordingly
					static_assert(JUCE_MAJOR_VERSION == 7 && JUCE_MINOR_VERSION == 0);
					const auto pixel = reinterpret_cast<juce::PixelARGB*>(bitmapData.getPixelPointer(x, y));

					buffer[bufferIndex++] = pixel->getRed();
					buffer[bufferIndex++] = pixel->getGreen();
					buffer[bufferIndex++] = pixel->getBlue();
					buffer[bufferIndex++] = pixel->getAlpha();
				}
			}
			break;
		case juce::Image::RGB:
			buffer.resize(pixelCount * 3);
			for (int y=0; y<h; ++y)
			{
				for (int x=0; x<w; ++x)
				{
					auto pixel = bitmapData.getPixelColour(x,y);
					buffer[bufferIndex++] = pixel.getRed();
					buffer[bufferIndex++] = pixel.getGreen();
					buffer[bufferIndex++] = pixel.getBlue();
				}
			}
			break;
		case juce::Image::SingleChannel:
			{
				buffer.resize(pixelCount * 3);

				uint8_t* pixelPtr = bitmapData.data;
				for (int i=0; i<pixelCount; ++i, ++pixelPtr)
				{
					buffer[bufferIndex++] = *pixelPtr;
					buffer[bufferIndex++] = *pixelPtr;
					buffer[bufferIndex++] = *pixelPtr;
				}
			}
			break;
		default:
			Rml::Log::Message(Rml::Log::LT_ERROR, "Unsupported image format: %d", static_cast<int>(_image.getFormat()));
			assert(false && "unsupported image format");
			return {};
		}

		auto dummyHandle = createDummyHandle();

		addRenderFunction([this, dummyHandle, b = std::move(buffer), w, h]
		{
			const auto handle = m_renderer->GenerateTexture(b, Rml::Vector2i(w, h));
			addHandle(dummyHandle, handle);
		});

		return dummyHandle;
	}

	void RendererProxy::executeRenderFunctions()
	{
		{
			std::lock_guard lock(m_mutexRender);

			if (!m_renderer)
				return;

			if (!m_renderFunctions.empty())
			{
				std::swap(m_renderFunctionsToExecute, m_renderFunctions.front());

				if (m_renderFunctions.size() == 1)
					m_renderFunctions.clear();
				else
					m_renderFunctions.erase(m_renderFunctions.begin());
			}
		}

		for (auto& func : m_renderFunctionsToExecute)
			func();
	}

	void RendererProxy::finishFrame()
	{
		std::lock_guard lockR(m_mutexRender);
		std::lock_guard lock(m_mutex);
		m_renderFunctions.emplace_back(std::move(m_enqueuedFunctions));
		m_enqueuedFunctions.clear();
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
		}

		m_renderer = _renderer;
	}

	bool RendererProxy::loadImage(juce::Image& _image, Rml::Vector2i& _textureDimensions, const Rml::String& _source) const
	{
		uint32_t fileSize;
		auto* ptr = m_dataProvider.getResourceByFilename(baseLib::filesystem::getFilenameWithoutPath(_source), fileSize);

		if (!ptr)
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "image file not found: %s", _source.c_str());
			assert(false && "file not found");
			return false;
		}

		// Load the texture from the file
		_image = juce::ImageFileFormat::loadFrom(ptr, fileSize);
		if (_image.isNull())
		{
			Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load image from source %s", _source.c_str());
			assert(false && "failed to load image");
			return false;
		}

		_textureDimensions.x = _image.getWidth();
		_textureDimensions.y = _image.getHeight();

		return true;
	}
}
