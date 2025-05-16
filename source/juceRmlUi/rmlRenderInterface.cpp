#include "rmlRenderInterface.h"

#include <cassert>

#include "rmlHelper.h"

#include "juce_opengl/juce_opengl.h"

namespace juceRmlUi
{
	using namespace juce::gl;

	static_assert(sizeof(Rml::CompiledGeometryHandle) == sizeof(void*), "handles must have size for a pointer");

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

	    glEnableClientState(GL_VERTEX_ARRAY);
	    glEnableClientState(GL_COLOR_ARRAY);
	    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

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

	    glDisableClientState(GL_VERTEX_ARRAY);
	    glDisableClientState(GL_COLOR_ARRAY);
	    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

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
		// Load the texture from the file
		auto image = juce::ImageFileFormat::loadFrom(juce::String(_source.c_str()));
		if (image.isNull())
			return 0;
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
			assert(false);
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
}
