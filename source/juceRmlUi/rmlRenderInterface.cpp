#include "rmlRenderInterface.h"

#include <cassert>

#include "rmlHelper.h"
#include "baseLib/filesystem.h"

#include "juce_opengl/juce_opengl.h"
#include "juce_gui_basics/juce_gui_basics.h"

#include "juceUiLib/editorInterface.h"

namespace juceRmlUi
{
	using namespace juce::gl;

	static_assert(sizeof(Rml::CompiledGeometryHandle) == sizeof(void*), "handles must have size for a pointer");

	RenderInterface::RenderInterface(genericUI::EditorInterface& _editorInterface) : m_editorInterface(_editorInterface)
	{
	}

	Rml::CompiledGeometryHandle RenderInterface::CompileGeometry(const Rml::Span<const Rml::Vertex> _vertices, const Rml::Span<const int> _indices)
	{
		auto* g = new CompiledGeometry();

		// vertex buffer
		glGenBuffers(1, &g->vertexBuffer);
		glBindBuffer(GL_ARRAY_BUFFER, g->vertexBuffer);
		glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(_vertices.size() * sizeof(Rml::Vertex)), _vertices.data(), GL_STATIC_DRAW);

		// index buffer
		glGenBuffers(1, &g->indexBuffer);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g->indexBuffer);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(_indices.size() * sizeof(int)), _indices.data(), GL_STATIC_DRAW);

		g->indexCount = _indices.size();

		return helper::toHandle<Rml::CompiledGeometryHandle>(g);
	}

	void RenderInterface::RenderGeometry(const Rml::CompiledGeometryHandle _geometry, const Rml::Vector2f _translation, const Rml::TextureHandle _texture)
	{
	    auto* geom = helper::fromHandle<CompiledGeometry>(_geometry);

	    if (!geom) 
			return;

	    glPushMatrix();
	    glTranslatef(_translation.x, _translation.y, 0.0f);

	    glBindBuffer(GL_ARRAY_BUFFER, geom->vertexBuffer);

		glVertexPointer(2, GL_FLOAT, sizeof(Rml::Vertex), reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, position)));  // NOLINT(performance-no-int-to-ptr)
	    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Rml::Vertex), reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, colour)));  // NOLINT(performance-no-int-to-ptr)
	    glTexCoordPointer(2, GL_FLOAT, sizeof(Rml::Vertex), reinterpret_cast<GLvoid*>(offsetof(Rml::Vertex, tex_coord)));  // NOLINT(performance-no-int-to-ptr)

	    if (_texture)
	    {
		    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(_texture));
			glEnable(GL_TEXTURE_2D);
	    }
	    else
	    {
		    glBindTexture(GL_TEXTURE_2D, 0);
			glDisable(GL_TEXTURE_2D);
	    }

	    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom->indexBuffer);
	    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(geom->indexCount), GL_UNSIGNED_INT, nullptr);

	    glPopMatrix();
	}

	void RenderInterface::ReleaseGeometry(const Rml::CompiledGeometryHandle _geometry)
	{
	    auto* geom = helper::fromHandle<CompiledGeometry>(_geometry);
		assert(geom);
		glDeleteBuffers(1, &geom->vertexBuffer);
		glDeleteBuffers(1, &geom->indexBuffer);
		delete geom;
	}

	Rml::TextureHandle RenderInterface::LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source)
	{
		uint32_t fileSize;
		auto* ptr = m_editorInterface.getResourceByFilename(baseLib::filesystem::getFilenameWithoutPath(_source), fileSize);

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
		glBindTexture(GL_TEXTURE_2D, textureId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _textureDimensions.x, _textureDimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.getPixelData());
		return textureId;
	}

	Rml::TextureHandle RenderInterface::GenerateTexture(const Rml::Span<const unsigned char> _source, const Rml::Vector2i _sourceDimensions)
	{
		GLuint textureId;
		glGenTextures(1, &textureId);
		glBindTexture(GL_TEXTURE_2D, textureId);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _sourceDimensions.x, _sourceDimensions.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, _source.data());
		return textureId;
	}

	void RenderInterface::ReleaseTexture(Rml::TextureHandle _texture)
	{
		if (_texture)
		{
			glDeleteTextures(1, reinterpret_cast<GLuint*>(&_texture));
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
	}

	void RenderInterface::SetScissorRegion(const Rml::Rectanglei _region)
	{
		glScissor(_region.Left(), _region.Top(), _region.Width(), _region.Height());
		glEnable(GL_SCISSOR_TEST);
	}

	Rml::LayerHandle RenderInterface::PushLayer()
	{
		assert(m_frameBufferWidth && m_frameBufferHeight && "framebuffer size not set");

		GLuint fbo = 0;
	    GLuint texture = 0;

	    glGenFramebuffers(1, &fbo);
	    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	    glGenTextures(1, &texture);
	    glBindTexture(GL_TEXTURE_2D, texture);
	    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(m_frameBufferWidth), static_cast<GLsizei>(m_frameBufferHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

	    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	    {
			assert(false && "failed to create framebuffer");
	        glBindFramebuffer(GL_FRAMEBUFFER, 0);
	        glDeleteFramebuffers(1, &fbo);
	        glDeleteTextures(1, &texture);
	        return {};
	    }

	    // Save the framebuffer and texture in a struct
	    auto* layer = new LayerHandleData{ fbo, texture };
		m_layers.push(layer);
	    return helper::toHandle<Rml::LayerHandle>(layer);
	}

	void RenderInterface::CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, Rml::Span<const unsigned long long> _filters)
	{
		const auto* src = helper::fromHandle<LayerHandleData>(_source);

		if (auto* dst = helper::fromHandle<LayerHandleData>(_destination))
			glBindFramebuffer(GL_FRAMEBUFFER, dst->framebuffer);
		else
			glBindFramebuffer(GL_FRAMEBUFFER, 0); // back to screen

		glPushMatrix();
		glLoadIdentity();

	    glDisableClientState(GL_VERTEX_ARRAY);
	    glDisableClientState(GL_COLOR_ARRAY);
	    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

		switch (_blendMode)
		{
		    case Rml::BlendMode::Blend:
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		    case Rml::BlendMode::Replace:
				glDisable(GL_BLEND);
				glBlendFunc(GL_DST_COLOR, GL_ZERO);
				break;
		    default:
				assert(false && "unsupported blend mode");
				break;
		}

		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, src->texture);

		const auto w = static_cast<float>(m_frameBufferWidth);
		const auto h = static_cast<float>(m_frameBufferHeight);

		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(0, 0);
		glTexCoord2f(1, 0); glVertex2f(w, 0);
		glTexCoord2f(1, 1); glVertex2f(w, h);
		glTexCoord2f(0, 1); glVertex2f(0, h);
		glEnd();

		glBindTexture(GL_TEXTURE_2D, 0);
		glDisable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);

	    glEnableClientState(GL_VERTEX_ARRAY);
	    glEnableClientState(GL_COLOR_ARRAY);
	    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		glPopMatrix();
	}

	void RenderInterface::PopLayer()
	{
		assert(!m_layers.empty());

		auto* layer = m_layers.top();

		glDeleteFramebuffers(1, &layer->framebuffer);
		glDeleteTextures(1, &layer->texture);

		m_layers.pop();

		if (m_layers.empty())
		{
			glBindFramebuffer(GL_FRAMEBUFFER, 0);
		}
		else
		{
			layer = m_layers.top();
			glBindFramebuffer(GL_FRAMEBUFFER, layer->framebuffer);
		}

		Rml::RenderInterface::PopLayer();
	}

	void RenderInterface::EnableClipMask(bool _enable)
	{
		if (_enable)
			glEnable(GL_STENCIL_TEST);
		else
			glDisable(GL_STENCIL_TEST);
	}

	void RenderInterface::RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation)
	{
		using Rml::ClipMaskOperation;

		const bool clear_stencil = (_operation == ClipMaskOperation::Set || _operation == ClipMaskOperation::SetInverse);
		if (clear_stencil)
		{
			// @performance Increment the reference value instead of clearing each time.
			glClear(GL_STENCIL_BUFFER_BIT);
		}

		GLint stencil_test_value = 0;
		glGetIntegerv(GL_STENCIL_REF, &stencil_test_value);

		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glStencilFunc(GL_ALWAYS, 1, static_cast<GLuint>(-1));

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

		RenderGeometry(_geometry, _translation, {});

		// Restore state
		// @performance Cache state so we don't toggle it unnecessarily.
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
		glStencilFunc(GL_EQUAL, stencil_test_value, static_cast<GLuint>(-1));
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
		assert(false && "compile filter not implemented");
		return Rml::RenderInterface::CompileFilter(_name, _parameters);
	}

	void RenderInterface::ReleaseFilter(Rml::CompiledFilterHandle _filter)
	{
		assert(false && "release filter not implemented");
		Rml::RenderInterface::ReleaseFilter(_filter);
	}

	Rml::CompiledShaderHandle RenderInterface::CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters)
	{
		assert(false && "compile shader not implemented");
		return Rml::RenderInterface::CompileShader(_name, _parameters);
	}

	void RenderInterface::ReleaseShader(Rml::CompiledShaderHandle _shader)
	{
		assert(false && "release shader not implemented");
		Rml::RenderInterface::ReleaseShader(_shader);
	}

	void RenderInterface::beginFrame(const uint32_t _width, const uint32_t _height)
	{
		m_frameBufferWidth = _width;
		m_frameBufferHeight = _height;

		glEnableClientState(GL_VERTEX_ARRAY);
	    glEnableClientState(GL_COLOR_ARRAY);
	    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	}

	void RenderInterface::endFrame()
	{
	    glDisableClientState(GL_VERTEX_ARRAY);
	    glDisableClientState(GL_COLOR_ARRAY);
	    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	}
}
