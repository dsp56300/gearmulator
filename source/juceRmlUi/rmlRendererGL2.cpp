#include "rmlRendererGL2.h"

#include <cassert>

#include "juceRmlComponent.h"
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
#if 1
		if (_enable)
			glEnable(GL_SCISSOR_TEST);
		else
#endif
			glDisable(GL_SCISSOR_TEST);
		m_scissorEnabled = _enable;
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::SetScissorRegion(Rml::Rectanglei _region)
	{
		verticalFlip(_region);
		m_scissorRegion = _region;

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
		auto* dst = helper::fromHandle<LayerHandleData>(_destination);
		if (dst)
		{
			dest = dst->framebuffer;
		}

		CHECK_OPENGL_ERROR;

		auto* geom = helper::fromHandle<CompiledGeometry>(m_fullScreenGeometry);
		assert(geom && "invalid full screen geometry handle");

		glDisable(GL_BLEND);

		if (false && !_filters.empty())
		{
			uint32_t tempIndex = 0;

			if (!src)
			{
				// we don't have a texture handle for the main back buffer. We have to copy it to a temp buffer
				copyFramebuffer(m_tempFrameBuffers.front().framebuffer, 0);
				src = &m_tempFrameBuffers.front();
				++tempIndex; // start with the second temp buffer
			}

			// The first filter needs _source as input. The last filter needs 'dest' as output
			// The filters inbetween render to temp framebuffers in ping-pong mode
			const auto* source = src;

			for (size_t i=0; i<_filters.size(); ++i)
			{
				auto& filterHandle = _filters[i];
				auto* filter = helper::fromHandle<CompiledShader>(filterHandle);
				assert(filter && "invalid filter handle");

				const bool isLast = i == _filters.size() - 1;

				const LayerHandleData* destFrameBuffer = dst;

				if (!isLast)
				{
					destFrameBuffer = &m_tempFrameBuffers[tempIndex];
					++tempIndex;
					if (tempIndex >= m_tempFrameBuffers.size())
						tempIndex = 0;
					glBindFramebuffer(GL_FRAMEBUFFER, destFrameBuffer->framebuffer);
				}

				if (filter->type == ShaderType::Blur)
				{
					renderBlur(*geom, filter, source, destFrameBuffer, _blendMode);
				}
				else
				{
					if (isLast)
					{
						glBindFramebuffer(GL_FRAMEBUFFER, dest);
						CHECK_OPENGL_ERROR;
						if (!dest)
							setupBlending(_blendMode);
					}
					auto params = filter->params;
					params.texture = source->texture;
					renderGeometry(*geom, m_shaders.getShader(filter->type), params);
				}

				CHECK_OPENGL_ERROR;

				source = destFrameBuffer;
			}
		}
		else
		{
			if (src)
			{
				if (!dest)
					setupBlending(_blendMode);
				glBindFramebuffer(GL_FRAMEBUFFER, dest);
				CHECK_OPENGL_ERROR;
				renderGeometry(*geom, m_shaders.getShader(ShaderType::Fullscreen), { src->texture, Rml::Matrix4f::Identity() });
			}
			else
			{
				copyFramebuffer(dest, src ? src->framebuffer : 0);
			}
		}

		glBindTexture(GL_TEXTURE_2D, 0);
		CHECK_OPENGL_ERROR;
		glDisable(GL_TEXTURE_2D);
		CHECK_OPENGL_ERROR;
		glEnable(GL_BLEND);
		CHECK_OPENGL_ERROR;
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		CHECK_OPENGL_ERROR;

		if (dst != m_layers.back())
			glBindFramebuffer(GL_FRAMEBUFFER, m_layers.back()->framebuffer);
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
		m_stencilEnabled = _enable;
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
		if (m_layers.empty())
		{
			assert(false && "no layers available");
			return {};
		}

		auto bounds = m_scissorRegion;

		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_layers.back()->framebuffer);
		CHECK_OPENGL_ERROR;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_tempFrameBuffers.front().framebuffer);
		CHECK_OPENGL_ERROR;

		if (m_scissorEnabled)
			glDisable(GL_SCISSOR_TEST);
		if (m_stencilEnabled)
			glDisable(GL_STENCIL_TEST);

		// Flip the image vertically, as that convention is used for textures, and move to origin.
		glBlitFramebuffer(
			bounds.Left(), bounds.Bottom(),
			bounds.Right(), bounds.Top(),
			0, 0,
			bounds.Width(), bounds.Height(),
			GL_COLOR_BUFFER_BIT, GL_NEAREST
		);

		if (m_scissorEnabled)
			glEnable(GL_SCISSOR_TEST);
		if (m_stencilEnabled)
			glEnable(GL_STENCIL_TEST);

		CHECK_OPENGL_ERROR;

		GLuint textureId;

		glGenTextures(1, &textureId);
		CHECK_OPENGL_ERROR;
		glBindTexture(GL_TEXTURE_2D, textureId);
		CHECK_OPENGL_ERROR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		CHECK_OPENGL_ERROR;

		const auto imgWidth = bounds.Width();
		const auto imgHeight = bounds.Height();

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, imgWidth, imgHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		CHECK_OPENGL_ERROR;

		glBindFramebuffer(GL_READ_FRAMEBUFFER, m_tempFrameBuffers.front().framebuffer);
		CHECK_OPENGL_ERROR;
		glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, imgWidth, imgHeight);
		CHECK_OPENGL_ERROR;

		writeFramebufferToFile("E:\\rmlui_savelayerastexture_" + std::to_string(textureId) + ".png");

		generateMipmaps();

		CHECK_OPENGL_ERROR;

		return textureId;
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

		generateMipmaps();

		return textureId;
	}

	void RendererGL2::beginFrame(const uint32_t _width, const uint32_t _height)
	{
		Renderer::beginFrame(_width, _height);

		m_scissorRegion.p0 = { 0, 0 };
		m_scissorRegion.p1 = { 0, 0 };

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

		glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

	void RendererGL2::renderBlur(const CompiledGeometry& _geom, const CompiledShader* _filter, const LayerHandleData* _source, const LayerHandleData* _target, Rml::BlendMode _blendMode)
	{
		assert(_filter->shader && "blur filter needs custom shader");

		auto params = _filter->params;

		for (uint32_t p=0; p<_filter->params.blurPasses; ++p)
		{
			// horizontal blur pass into temp
			if (m_blurFrameBuffers.empty())
			{
				m_blurFrameBuffers.resize(1);
				createFrameBuffer(m_blurFrameBuffers[0]);
			}

			glBindFramebuffer(GL_FRAMEBUFFER, m_blurFrameBuffers[0].framebuffer);
			CHECK_OPENGL_ERROR;

			params.texture = _source->texture;

			params.blurScale.x = 1.0f / static_cast<float>(frameBufferWidth());
			params.blurScale.y = 0.0f;
			renderGeometry(_geom, *_filter->shader, params);

			// vertical pass into _target if last, into second temp otherwise
			const auto isLast = p == _filter->params.blurPasses - 1;
			if (isLast)
			{
				glBindFramebuffer(GL_FRAMEBUFFER, _target ? _target->framebuffer : 0);
				CHECK_OPENGL_ERROR;
				if (!_target)
					setupBlending(_blendMode);
			}
			else
			{
				if (m_blurFrameBuffers.size() < 2)
				{
					m_blurFrameBuffers.resize(2);
					createFrameBuffer(m_blurFrameBuffers[1]);
				}
				glBindFramebuffer(GL_FRAMEBUFFER, m_blurFrameBuffers[1].framebuffer);
				CHECK_OPENGL_ERROR;
			}

			params.texture = m_blurFrameBuffers[0].texture;

			params.blurScale.x = 0.0f;
			params.blurScale.y = 1.0f / static_cast<float>(frameBufferHeight());
			renderGeometry(_geom, *_filter->shader, params);

			if (m_blurFrameBuffers.size() > 1)
				_source = &m_blurFrameBuffers[1]; // next pass will use the target as source
		}
	}

	void RendererGL2::onResize()
	{
		const std::vector fullScreenVertices(std::begin(g_fullScreenVertices), std::end(g_fullScreenVertices));
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

		for (auto& f : m_blurFrameBuffers)
			deleteFrameBuffer(f);
		m_blurFrameBuffers.clear(); // recreated only when needed

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
		assert(frameBufferWidth() && frameBufferHeight() && "framebuffer size not set");

		GLuint fbo = 0;
	    GLuint texture = 0;
		GLuint stencil = 0;

	    glGenFramebuffers(1, &fbo);
		CHECK_OPENGL_ERROR;
	    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		CHECK_OPENGL_ERROR;

	    glGenTextures(1, &texture);
		CHECK_OPENGL_ERROR;
	    glBindTexture(GL_TEXTURE_2D, texture);
		CHECK_OPENGL_ERROR;
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(frameBufferWidth()), static_cast<GLsizei>(frameBufferHeight()), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		CHECK_OPENGL_ERROR;
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		CHECK_OPENGL_ERROR;

		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
		CHECK_OPENGL_ERROR;

		glGenRenderbuffers(1, &stencil);
		CHECK_OPENGL_ERROR;
		glBindRenderbuffer(GL_RENDERBUFFER, stencil);
		CHECK_OPENGL_ERROR;
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, static_cast<GLsizei>(frameBufferWidth()), static_cast<GLsizei>(frameBufferHeight()));
		CHECK_OPENGL_ERROR;
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, stencil);
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
			glDeleteRenderbuffers(1, &stencil);
			CHECK_OPENGL_ERROR;
	        return false;
	    }

		_layer.framebuffer = fbo;
		_layer.texture = texture;
		_layer.stencilBuffer = stencil;
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
		glDeleteRenderbuffers(1, &_layer.stencilBuffer);
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::generateMipmaps()
	{
		if (!glGenerateMipmap)
			return;
		glGenerateMipmap(GL_TEXTURE_2D);
		CHECK_OPENGL_ERROR;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		CHECK_OPENGL_ERROR;
	}
	void RendererGL2::copyFramebuffer(const uint32_t _dest, const uint32_t _source) const
	{
		glBindFramebuffer(GL_READ_FRAMEBUFFER, _source);
		CHECK_OPENGL_ERROR;
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _dest);
		CHECK_OPENGL_ERROR;
		const auto w = static_cast<GLint>(frameBufferWidth());
		const auto h = static_cast<GLint>(frameBufferHeight());
		glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		CHECK_OPENGL_ERROR;
	}

	void RendererGL2::setupBlending(Rml::BlendMode _blendMode)
	{
		switch (_blendMode)
		{
		    case Rml::BlendMode::Blend:
				glEnable(GL_BLEND);
				CHECK_OPENGL_ERROR;
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
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
	}

	void RendererGL2::writeFramebufferToFile(const std::string& _name, const uint32_t _x, const uint32_t _y, const uint32_t _width, const uint32_t _height) const
	{
		const auto x = static_cast<int>(_x);
		const auto y = static_cast<int>(_y);
		const auto w = static_cast<int>(_width ? _width : frameBufferWidth());
		const auto h = static_cast<int>(_height ? _height : frameBufferHeight());

		juce::Image img(juce::Image::ARGB, w, h, false);
		juce::Image::BitmapData bitmapData(img, 0, 0, w, h, juce::Image::BitmapData::writeOnly);

		glReadPixels(x, y, w, h, GL_BGRA, GL_UNSIGNED_BYTE, bitmapData.data);
		CHECK_OPENGL_ERROR;

		juce::File file(_name);
		file.deleteFile();
		file.create();
		juce::PNGImageFormat pngFormat;
		auto filestream = file.createOutputStream();

		pngFormat.writeImageToStream(img, *filestream);
		filestream->flush();
	}
}
