#include "rmlRendererGL2.h"

#include <cassert>

#include "rmlHelper.h"
#include "rmlRendererGL2Types.h"
#include "baseLib/filesystem.h"

#include "juce_opengl/juce_opengl.h"

namespace juceRmlUi::gl2
{
	using namespace juce::gl;

	static_assert(sizeof(Rml::CompiledGeometryHandle) == sizeof(void*), "handles must have size for a pointer");

	namespace
	{
		const Rml::Vertex g_fullScreenVertices[] = {
			{{-1,1}, {255,255}, {0,1}},
			{{1,1}, {255,255}, {1,1}},
			{{1,-1}, {255,255}, {1,0}},
			{{-1,-1}, {255,255}, {0,0}}
		};

		const int g_fullScreenIndices[] = {0,1,2,2,3,0};
	}

	RendererGL2::RendererGL2(DataProvider& _dataProvider) : Renderer(_dataProvider), m_filters(m_shaders)
	{
	}

	Rml::CompiledGeometryHandle RendererGL2::CompileGeometry(const Rml::Span<const Rml::Vertex> _vertices, const Rml::Span<const int> _indices)
	{
		auto* g = new CompiledGeometry();

		// vertex buffer
		glGenBuffers(1, &g->vertexBuffer);
		CHECK_OPENGL_ERROR;
		glBindBuffer(GL_ARRAY_BUFFER, g->vertexBuffer);
		CHECK_OPENGL_ERROR;
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_vertices.size() * sizeof(Rml::Vertex)), _vertices.data(), GL_STATIC_DRAW);
		CHECK_OPENGL_ERROR;

		// index buffer
		glGenBuffers(1, &g->indexBuffer);
		CHECK_OPENGL_ERROR;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->indexBuffer);
		CHECK_OPENGL_ERROR;
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(_indices.size() * sizeof(int)), _indices.data(), GL_STATIC_DRAW);
		CHECK_OPENGL_ERROR;

		g->indexCount = _indices.size();

		return helper::toHandle<Rml::CompiledGeometryHandle>(g);
	}

	void RendererGL2::RenderGeometry(const Rml::CompiledGeometryHandle _geometry, const Rml::Vector2f _translation, const Rml::TextureHandle _texture)
	{
		CHECK_OPENGL_ERROR;

	    auto* geom = helper::fromHandle<CompiledGeometry>(_geometry);

	    if (!geom) 
			return;

	    glPushMatrix();
		CHECK_OPENGL_ERROR;
	    glTranslatef(_translation.x, _translation.y, 0.0f);
		CHECK_OPENGL_ERROR;

		auto& shader = m_shaders.getShader(_texture ? ShaderType::DefaultTextured : ShaderType::DefaultColored);

		renderGeometry(*geom, shader, { static_cast<uint32_t>(_texture), Rml::Matrix4f::Identity() });

	    glPopMatrix();
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::ReleaseGeometry(const Rml::CompiledGeometryHandle _geometry)
	{
	    auto* geom = helper::fromHandle<CompiledGeometry>(_geometry);
		assert(geom);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		CHECK_OPENGL_ERROR;
		glDeleteBuffers(1, &geom->vertexBuffer);
		CHECK_OPENGL_ERROR;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		CHECK_OPENGL_ERROR;
		glDeleteBuffers(1, &geom->indexBuffer);
		CHECK_OPENGL_ERROR;
		delete geom;
	}

	Rml::TextureHandle RendererGL2::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		juce::Image image;
		if (!loadImage(image, _textureDimensions, _source))
			return {};
		return loadTexture(image);
	}

	Rml::TextureHandle RendererGL2::GenerateTexture(const Rml::Span<const unsigned char> _source, const Rml::Vector2i _sourceDimensions)
	{
		GLuint textureId;
		glGenTextures(1, &textureId);
		CHECK_OPENGL_ERROR;
		glBindTexture(GL_TEXTURE_2D, textureId);
		CHECK_OPENGL_ERROR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _sourceDimensions.x, _sourceDimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, _source.data());
		CHECK_OPENGL_ERROR;
		return textureId;
	}

	void RendererGL2::ReleaseTexture(Rml::TextureHandle _texture)
	{
		if (_texture)
		{
			glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture));
			CHECK_OPENGL_ERROR;
		}
		else
		{
			assert(false && "invalid texture handle to delete");
		}
	}

	void RendererGL2::EnableScissorRegion(const bool _enable)
	{
		if (_enable)
			glEnable(GL_SCISSOR_TEST);
		else
			glDisable(GL_SCISSOR_TEST);
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::SetScissorRegion(Rml::Rectanglei _region)
	{
		_region.p0.y = static_cast<int>(m_frameBufferHeight) - _region.p0.y;
		_region.p1.y = static_cast<int>(m_frameBufferHeight) - _region.p1.y;

		if (_region.p0.y > _region.p1.y)
			std::swap(_region.p0.y, _region.p1.y);

		auto x = _region.Left();
		auto y = _region.Top();
		auto width = _region.Width();
		auto height = _region.Height();
		glScissor(x,y,width, height);
		CHECK_OPENGL_ERROR;
//		glEnable(GL_SCISSOR_TEST);
	}

	Rml::LayerHandle RendererGL2::PushLayer()
	{
		auto layer = createFrameBuffer();
		m_layers.push_back(layer);
	    return helper::toHandle<Rml::LayerHandle>(layer);
	}

	void RendererGL2::CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, const Rml::Span<const Rml::CompiledFilterHandle> _filters)
	{
		CHECK_OPENGL_ERROR;

		const auto* src = helper::fromHandle<LayerHandleData>(_source);

		GLuint dest = 0;

		// if this is not set, target is the screen
		if (auto* dst = helper::fromHandle<LayerHandleData>(_destination))
		{
			dest = dst->framebuffer;
		}

		CHECK_OPENGL_ERROR;

		switch (_blendMode)
		{
		    case Rml::BlendMode::Blend:
				glEnable(GL_BLEND);
				CHECK_OPENGL_ERROR;
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				CHECK_OPENGL_ERROR;
			break;
		    case Rml::BlendMode::Replace:
				glDisable(GL_BLEND);
				CHECK_OPENGL_ERROR;
				glBlendFunc(GL_DST_COLOR, GL_ZERO);
				CHECK_OPENGL_ERROR;
				break;
		    default:
				assert(false && "unsupported blend mode");
				break;
		}

		auto* geom = helper::fromHandle<CompiledGeometry>(m_fullScreenGeometry);
		assert(geom && "invalid full screen geometry handle");

		if (!_filters.empty())
		{
			// The first filter needs _source as input. The last filter needs 'dest' as output
			// The filters inbetween render to temp framebuffers in ping-pong mode
			const auto* source = src;

			uint32_t tempIndex = 0;

			for (size_t i=0; i<_filters.size(); ++i)
			{
				auto& filterHandle = _filters[i];
				auto* filter = helper::fromHandle<CompiledShader>(filterHandle);
				assert(filter && "invalid filter handle");

				const bool isLast = i == _filters.size() - 1;

				const LayerHandleData* destFrameBuffer = nullptr;

				if (isLast)
				{
					glBindFramebuffer(GL_FRAMEBUFFER, dest);
				}
				else
				{
					destFrameBuffer = &m_tempFrameBuffers[tempIndex];
					++tempIndex;
					if (tempIndex >= m_tempFrameBuffers.size())
						tempIndex = 0;
					glBindFramebuffer(GL_FRAMEBUFFER, destFrameBuffer->framebuffer);
					glClearColor(0.5f, 0.5f, 0.5f, 0.0f);
					glClear(GL_COLOR_BUFFER_BIT);
				}

				auto params = filter->params;
				params.texture = source->texture;
				renderGeometry(*geom, m_shaders.getShader(filter->type), params);

				source = destFrameBuffer;
			}
		}
		else
		{
			glBindFramebuffer(GL_FRAMEBUFFER, dest);

			auto& shader = m_shaders.getShader(ShaderType::FullscreenColorMatrix);	// FIXME: color matrix not needed

			renderGeometry(*geom, shader, { src->texture, Rml::Matrix4f::Identity() });
		}

		glBindTexture(GL_TEXTURE_2D, 0);
		CHECK_OPENGL_ERROR;
		glDisable(GL_TEXTURE_2D);
		CHECK_OPENGL_ERROR;
		glEnable(GL_BLEND);
		CHECK_OPENGL_ERROR;
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::PopLayer()
	{
		assert(!m_layers.empty());

		const auto* layer = m_layers.back();
		m_layers.pop_back();

		deleteFrameBuffer(layer);

		if (m_layers.empty())
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			CHECK_OPENGL_ERROR;
		}
		else
		{
			layer = m_layers.back();
			glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer);
			CHECK_OPENGL_ERROR;
		}

		RenderInterface::PopLayer();
	}

	void RendererGL2::EnableClipMask(bool _enable)
	{
		if (_enable)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation)
	{
		using Rml::ClipMaskOperation;

		if (_operation == ClipMaskOperation::Set || _operation == ClipMaskOperation::SetInverse)
		{
			// @performance Increment the reference value instead of clearing each time.
			glClear(GL_STENCIL_BUFFER_BIT);
			CHECK_OPENGL_ERROR;
		}

		GLint stencil_test_value = 0;
		glGetIntegerv(GL_STENCIL_REF, &stencil_test_value);
		CHECK_OPENGL_ERROR;

		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		CHECK_OPENGL_ERROR;
		glStencilFunc(GL_ALWAYS, 1, static_cast<GLuint>(-1));
		CHECK_OPENGL_ERROR;

		switch (_operation)
		{
		case ClipMaskOperation::Set:
		{
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			stencil_test_value = 1;
		}
		break;
		case ClipMaskOperation::SetInverse:
		{
			glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
			stencil_test_value = 0;
		}
		break;
		case ClipMaskOperation::Intersect:
		{
			glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
			stencil_test_value += 1;
		}
		break;
		}

		CHECK_OPENGL_ERROR;

		RenderGeometry(_geometry, _translation, {});

		// Restore state
		// @performance Cache state so we don't toggle it unnecessarily.
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		CHECK_OPENGL_ERROR;
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		CHECK_OPENGL_ERROR;
		glStencilFunc(GL_EQUAL, stencil_test_value, static_cast<GLuint>(-1));
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::SetTransform(const Rml::Matrix4f* _transform)
	{
		if (_transform)
		{
			if constexpr(std::is_same_v<Rml::Matrix4f, Rml::ColumnMajorMatrix4f>)
				glLoadMatrixf(_transform->data());
			else if constexpr(std::is_same_v<Rml::Matrix4f, Rml::RowMajorMatrix4f>)
				glLoadMatrixf(_transform->Transpose().data());
		}
		else
		{
			glLoadIdentity();
		}

		CHECK_OPENGL_ERROR;
	}

	Rml::TextureHandle RendererGL2::SaveLayerAsTexture()
	{
		assert(false && "save layer as texture not implemented");
		return RenderInterface::SaveLayerAsTexture();
	}

	Rml::CompiledFilterHandle RendererGL2::SaveLayerAsMaskImage()
	{
		assert(false && "save layer as mask image not implemented");
		return RenderInterface::SaveLayerAsMaskImage();
	}

	Rml::CompiledFilterHandle RendererGL2::CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto program = m_filters.create(_name, _parameters);
		return helper::toHandle<Rml::CompiledFilterHandle>(program);
	}

	void RendererGL2::ReleaseFilter(Rml::CompiledFilterHandle _filter)
	{
		const auto* filter = helper::fromHandle<CompiledShader>(_filter);
		delete filter;
	}

	Rml::CompiledShaderHandle RendererGL2::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto prog = m_shaders.create(_name, _parameters);
		return helper::toHandle<Rml::CompiledShaderHandle>(prog);
	}

	void RendererGL2::ReleaseShader(Rml::CompiledShaderHandle _shader)
	{
		auto* shader = helper::fromHandle<CompiledShader>(_shader);
		delete shader;
	}

	Rml::TextureHandle RendererGL2::loadTexture(juce::Image& _image)
	{
		// Create a new OpenGL texture
		GLuint textureId;
		glGenTextures(1, &textureId);
		CHECK_OPENGL_ERROR;
		glBindTexture(GL_TEXTURE_2D, textureId);
		CHECK_OPENGL_ERROR;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;

		const auto width = _image.getWidth();
		const auto height = _image.getHeight();

		juce::Image::BitmapData bitmapData(_image, 0, 0, width, height, juce::Image::BitmapData::readOnly);
		const auto* pixelPtr = bitmapData.getPixelPointer(0,0);

		switch(_image.getFormat())
		{
		case juce::Image::ARGB:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixelPtr);
			CHECK_OPENGL_ERROR;
			break;
		case juce::Image::RGB:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_BGR, GL_UNSIGNED_BYTE, pixelPtr);
			CHECK_OPENGL_ERROR;
			break;
		default:
			assert(false && "unsupported image format");
		}

		if (glGenerateMipmap)
		{
			glGenerateMipmap(GL_TEXTURE_2D);
			CHECK_OPENGL_ERROR;
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			CHECK_OPENGL_ERROR;
		}

		return textureId;
	}

	void RendererGL2::beginFrame(const uint32_t _width, const uint32_t _height)
	{
		if (_width != m_frameBufferWidth || _height != m_frameBufferHeight)
		{
			m_frameBufferWidth = _width;
			m_frameBufferHeight = _height;

			onResize();
		}

		glDisable(GL_DEPTH_TEST);
		CHECK_OPENGL_ERROR;
		glDisable(GL_LIGHTING);
		CHECK_OPENGL_ERROR;
		glDisable(GL_CULL_FACE);
		CHECK_OPENGL_ERROR;
		glEnable(GL_BLEND);
		CHECK_OPENGL_ERROR;
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::endFrame()
	{
	}

	void RendererGL2::checkGLError(const char* _file, const int _line)
	{
		for (;;)
		{
			const GLenum e = glGetError();

			if (e == GL_NO_ERROR)
				break;

			Rml::Log::Message (Rml::Log::LT_WARNING, "OpenGL ERROR %u in file %s, line %d", e, _file, _line);
			assert(false);
		}
	}

	void RendererGL2::renderGeometry(const CompiledGeometry& _geom, RmlShader& _shader, const ShaderParams& _shaderParams)
	{
		glBindBuffer(GL_ARRAY_BUFFER, _geom.vertexBuffer);
		CHECK_OPENGL_ERROR;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _geom.indexBuffer);
		CHECK_OPENGL_ERROR;

		_shader.enableShader(_shaderParams);

		_shader.setupVertexAttributes();

	    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(_geom.indexCount), GL_UNSIGNED_INT, nullptr);
		CHECK_OPENGL_ERROR;

		_shader.disableShader();
	}

	void RendererGL2::onResize()
	{
		std::vector fullScreenVertices(std::begin(g_fullScreenVertices), std::end(g_fullScreenVertices));

		for(auto & v : fullScreenVertices)
		{
//			v.position.x *= static_cast<float>(m_frameBufferWidth);
//			v.position.y *= static_cast<float>(m_frameBufferHeight);
		}

		const std::vector fullScreenIndices(std::begin(g_fullScreenIndices), std::end(g_fullScreenIndices));

		if (m_fullScreenGeometry)
			ReleaseGeometry(m_fullScreenGeometry);

		m_fullScreenGeometry = CompileGeometry(fullScreenVertices, fullScreenIndices);

		for (const auto& layer : m_layers)
		{
			deleteFrameBuffer(*layer);
			createFrameBuffer(*layer);
		}

		for (auto& tempFrameBuffer : m_tempFrameBuffers)
			deleteFrameBuffer(tempFrameBuffer);

		m_tempFrameBuffers.resize(2);

		for (size_t i=0; i<2; ++i)
			createFrameBuffer(m_tempFrameBuffers[i]);
	}

	LayerHandleData* RendererGL2::createFrameBuffer() const
	{
		auto* layer = new LayerHandleData();

		if (!createFrameBuffer(*layer))
		{
			delete layer;
			return nullptr;
		}
		return layer;
	}

	bool RendererGL2::createFrameBuffer(LayerHandleData& _layer) const
	{
		assert(m_frameBufferWidth && m_frameBufferHeight && "framebuffer size not set");

		GLuint fbo = 0;
	    GLuint texture = 0;

	    glGenFramebuffers(1, &fbo);
		CHECK_OPENGL_ERROR;
	    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		CHECK_OPENGL_ERROR;

	    glGenTextures(1, &texture);
		CHECK_OPENGL_ERROR;
	    glBindTexture(GL_TEXTURE_2D, texture);
		CHECK_OPENGL_ERROR;
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(m_frameBufferWidth), static_cast<GLsizei>(m_frameBufferHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		CHECK_OPENGL_ERROR;
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;

	    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		CHECK_OPENGL_ERROR;

	    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	    {
			CHECK_OPENGL_ERROR;
			assert(false && "failed to create framebuffer");
	        glBindFramebuffer(GL_FRAMEBUFFER, 0);
			CHECK_OPENGL_ERROR;
	        glDeleteFramebuffers(1, &fbo);
			CHECK_OPENGL_ERROR;
	        glDeleteTextures(1, &texture);
			CHECK_OPENGL_ERROR;
	        return false;
	    }

		_layer.framebuffer = fbo;
		_layer.texture = texture;
		return true;
	}

	void RendererGL2::deleteFrameBuffer(const LayerHandleData*& _layer)
	{
		if (!_layer)
			return;
		deleteFrameBuffer(*_layer);
		delete _layer;
		_layer = nullptr;
	}
	void RendererGL2::deleteFrameBuffer(const LayerHandleData& _layer)
	{
		glDeleteFramebuffers(1, &_layer.framebuffer);
		CHECK_OPENGL_ERROR;
		glDeleteTextures(1, &_layer.texture);
		CHECK_OPENGL_ERROR;
	}
}
