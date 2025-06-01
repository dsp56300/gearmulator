#include "rmlRenderInterface.h"

#include <cassert>

#include "rmlDataProvider.h"
#include "rmlHelper.h"
#include "baseLib/filesystem.h"

#include "juce_opengl/juce_opengl.h"
#include "juce_gui_basics/juce_gui_basics.h"

namespace juceRmlUi
{
	using namespace juce::gl;

	static_assert(sizeof(Rml::CompiledGeometryHandle) == sizeof(void*), "handles must have size for a pointer");

	namespace
	{
		void checkGLError (const char* file, const int line)
		{
		    for (;;)
		    {
		        const GLenum e = glGetError();

		        if (e == GL_NO_ERROR)
		            break;

		        Rml::Log::Message (Rml::Log::LT_WARNING, "OpenGL ERROR %u", e);
		        assert(false);
		    }
		}

		#define CHECK_OPENGL_ERROR do { checkGLError (__FILE__, __LINE__); } while(0)
	}
	RenderInterface::RenderInterface(DataProvider& _dataProvider) : m_dataProvider(_dataProvider), m_filters(m_shaders)
	{
	}

	Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(const Rml::Span<const Rml::Vertex> _vertices, const Rml::Span<const int> _indices)
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

	void RenderInterface::RenderGeometry(const Rml::CompiledGeometryHandle _geometry, const Rml::Vector2f _translation, const Rml::TextureHandle _texture)
	{
		CHECK_OPENGL_ERROR;

	    auto* geom = helper::fromHandle<CompiledGeometry>(_geometry);

	    if (!geom) 
			return;

	    glPushMatrix();
		CHECK_OPENGL_ERROR;
	    glTranslatef(_translation.x, _translation.y, 0.0f);
		CHECK_OPENGL_ERROR;

	    glBindBuffer(GL_ARRAY_BUFFER, geom->vertexBuffer);
		CHECK_OPENGL_ERROR;

		glVertexPointer(2, GL_FLOAT, sizeof(Rml::Vertex), reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, position)));  // NOLINT(performance-no-int-to-ptr)
		CHECK_OPENGL_ERROR;
	    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Rml::Vertex), reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, colour)));  // NOLINT(performance-no-int-to-ptr)
		CHECK_OPENGL_ERROR;
	    glTexCoordPointer(2, GL_FLOAT, sizeof(Rml::Vertex), reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, tex_coord)));  // NOLINT(performance-no-int-to-ptr)
		CHECK_OPENGL_ERROR;

	    if (_texture)
	    {
		    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_texture));
			CHECK_OPENGL_ERROR;
			glEnable(GL_TEXTURE_2D);
			CHECK_OPENGL_ERROR;
	    }
	    else
	    {
		    glBindTexture(GL_TEXTURE_2D, 0);
			CHECK_OPENGL_ERROR;
			glDisable(GL_TEXTURE_2D);
			CHECK_OPENGL_ERROR;
	    }

	    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom->indexBuffer);
		CHECK_OPENGL_ERROR;
	    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(geom->indexCount), GL_UNSIGNED_INT, nullptr);
		CHECK_OPENGL_ERROR;

	    glPopMatrix();
		CHECK_OPENGL_ERROR;
	}

	void RenderInterface::ReleaseGeometry(const Rml::CompiledGeometryHandle _geometry)
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

	Rml::TextureHandle RenderInterface::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		uint32_t fileSize;
		auto* ptr = m_dataProvider.getResourceByFilename(baseLib::filesystem::getFilenameWithoutPath(_source), fileSize);

		if (!ptr)
		{
			assert(false && "file not found");
			return {};
		}

		// Load the texture from the file
		auto image = juce::ImageFileFormat::loadFrom(ptr, fileSize);
		if (image.isNull())
		{
			assert(false && "failed to load image");
			return {};
		}
		// Get the texture dimensions
		_textureDimensions.x = image.getWidth();
		_textureDimensions.y = image.getHeight();
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

		juce::Image::BitmapData bitmapData(image, 0, 0, image.getWidth(), image.getHeight(), juce::Image::BitmapData::readOnly);
		const auto* pixelPtr = bitmapData.getPixelPointer(0,0);

		switch(image.getFormat())
		{
		case juce::Image::ARGB:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _textureDimensions.x, _textureDimensions.y, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixelPtr);
			CHECK_OPENGL_ERROR;
			break;
		case juce::Image::RGB:
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, _textureDimensions.x, _textureDimensions.y, 0, GL_BGR, GL_UNSIGNED_BYTE, pixelPtr);
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
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			CHECK_OPENGL_ERROR;
		}

		return textureId;
	}

	Rml::TextureHandle RenderInterface::GenerateTexture(const Rml::Span<const unsigned char> _source, const Rml::Vector2i _sourceDimensions)
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

	void RenderInterface::ReleaseTexture(Rml::TextureHandle _texture)
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

	void RenderInterface::EnableScissorRegion(const bool _enable)
	{
		if (_enable)
			glEnable(GL_SCISSOR_TEST);
		else
			glDisable(GL_SCISSOR_TEST);
		CHECK_OPENGL_ERROR;
	}

	void RenderInterface::SetScissorRegion(Rml::Rectanglei _region)
	{
		_region.p0.y = static_cast<int>(m_frameBufferHeight) - _region.p1.y;
		_region.p1.y = static_cast<int>(m_frameBufferHeight) - _region.p0.y;

		const auto y0 = _region.Top();
		const auto y1 = _region.Top() + _region.Height();

		auto x = _region.Left();
		auto y = std::min(y0, y1);
		auto width = _region.Width();
		auto height = std::max(y0, y1) - y;
		glScissor(x,y,width, height);
		CHECK_OPENGL_ERROR;
//		glEnable(GL_SCISSOR_TEST);
	}

	Rml::LayerHandle RenderInterface::PushLayer()
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
	        return {};
	    }

	    // Save the framebuffer and texture in a struct
	    auto* layer = new LayerHandleData{ fbo, texture };
		m_layers.push(layer);
	    return helper::toHandle<Rml::LayerHandle>(layer);
	}

	void RenderInterface::CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, Rml::Span<const Rml::CompiledFilterHandle> _filters)
	{
		CHECK_OPENGL_ERROR;

		const auto* src = helper::fromHandle<LayerHandleData>(_source);

		if (auto* dst = helper::fromHandle<LayerHandleData>(_destination))
			glBindFramebuffer(GL_FRAMEBUFFER, dst->framebuffer);
		else
			glBindFramebuffer(GL_FRAMEBUFFER, 0); // back to screen
		CHECK_OPENGL_ERROR;

		glPushMatrix();
		CHECK_OPENGL_ERROR;
		glLoadIdentity();
		CHECK_OPENGL_ERROR;
	    glDisableClientState(GL_VERTEX_ARRAY);
		CHECK_OPENGL_ERROR;
	    glDisableClientState(GL_COLOR_ARRAY);
		CHECK_OPENGL_ERROR;
	    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
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

		glEnable(GL_TEXTURE_2D);
		CHECK_OPENGL_ERROR;
		glBindTexture(GL_TEXTURE_2D, src->texture);
		CHECK_OPENGL_ERROR;

		const auto w = static_cast<float>(m_frameBufferWidth);
		const auto h = static_cast<float>(m_frameBufferHeight);

		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(0, h);
		glTexCoord2f(1, 0); glVertex2f(w, h);
		glTexCoord2f(1, 1); glVertex2f(w, 0);
		glTexCoord2f(0, 1); glVertex2f(0, 0);
		glEnd();

		glBindTexture(GL_TEXTURE_2D, 0);
		CHECK_OPENGL_ERROR;
		glDisable(GL_TEXTURE_2D);
		CHECK_OPENGL_ERROR;
		glEnable(GL_BLEND);
		CHECK_OPENGL_ERROR;
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		CHECK_OPENGL_ERROR;

	    glEnableClientState(GL_VERTEX_ARRAY);
		CHECK_OPENGL_ERROR;
	    glEnableClientState(GL_COLOR_ARRAY);
		CHECK_OPENGL_ERROR;
	    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_OPENGL_ERROR;
		glPopMatrix();
		CHECK_OPENGL_ERROR;
	}

	void RenderInterface::PopLayer()
	{
		assert(!m_layers.empty());

		const auto* layer = m_layers.top();

		glDeleteFramebuffers(1, &layer->framebuffer);
		CHECK_OPENGL_ERROR;
		glDeleteTextures(1, &layer->texture);
		CHECK_OPENGL_ERROR;

		m_layers.pop();

		if (m_layers.empty())
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
			CHECK_OPENGL_ERROR;
		}
		else
		{
			layer = m_layers.top();
			glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer);
			CHECK_OPENGL_ERROR;
		}

		Rml::RenderInterface::PopLayer();
	}

	void RenderInterface::EnableClipMask(bool _enable)
	{
		if (_enable)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
		CHECK_OPENGL_ERROR;
	}

	void RenderInterface::RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation)
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

	void RenderInterface::SetTransform(const Rml::Matrix4f* _transform)
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

	Rml::TextureHandle RenderInterface::SaveLayerAsTexture()
	{
		assert(false && "save layer as texture not implemented");
		return Rml::RenderInterface::SaveLayerAsTexture();
	}

	Rml::CompiledFilterHandle RenderInterface::SaveLayerAsMaskImage()
	{
		assert(false && "save layer as mask image not implemented");
		return Rml::RenderInterface::SaveLayerAsMaskImage();
	}

	Rml::CompiledFilterHandle RenderInterface::CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto program = m_filters.create(_name, _parameters);
		return helper::toHandle<Rml::CompiledFilterHandle>(new CompiledShader{program});
	}

	void RenderInterface::ReleaseFilter(Rml::CompiledFilterHandle _filter)
	{
		auto* filter = helper::fromHandle<CompiledShader>(_filter);
		glDeleteProgram(filter->program);
		CHECK_OPENGL_ERROR;
		delete filter;
	}

	Rml::CompiledShaderHandle RenderInterface::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		auto prog = m_shaders.create(_name, _parameters);
		return helper::toHandle<Rml::CompiledShaderHandle>(new CompiledShader{prog});
	}

	void RenderInterface::ReleaseShader(Rml::CompiledShaderHandle _shader)
	{
		auto* shader = helper::fromHandle<CompiledShader>(_shader);
		glDeleteProgram(shader->program);
		CHECK_OPENGL_ERROR;
		delete shader;
	}

	void RenderInterface::beginFrame(const uint32_t _width, const uint32_t _height)
	{
		m_frameBufferWidth = _width;
		m_frameBufferHeight = _height;

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

		glEnableClientState(GL_VERTEX_ARRAY);
		CHECK_OPENGL_ERROR;
	    glEnableClientState(GL_COLOR_ARRAY);
		CHECK_OPENGL_ERROR;
	    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_OPENGL_ERROR;
	}

	void RenderInterface::endFrame()
	{
	    glDisableClientState(GL_VERTEX_ARRAY);
		CHECK_OPENGL_ERROR;
	    glDisableClientState(GL_COLOR_ARRAY);
		CHECK_OPENGL_ERROR;
	    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
		CHECK_OPENGL_ERROR;
	}
}
