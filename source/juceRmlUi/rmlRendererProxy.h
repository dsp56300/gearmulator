#pragma once

#include <mutex>

#include "rmlRenderer.h"

namespace juceRmlUi
{
	class RendererProxy : public Renderer
	{
	public:
		using Func = std::function<void()>;
		using Handle = uintptr_t;
		static constexpr Handle InvalidHandle = 0;

		explicit RendererProxy(Renderer& _renderer);
		~RendererProxy() override;

		Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> _vertices, Rml::Span<const int> _indices) override;
		void RenderGeometry(Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;
		void ReleaseGeometry(Rml::CompiledGeometryHandle _geometry) override;
		Rml::TextureHandle LoadTexture(Rml::Vector2i& _textureDimensions, const Rml::String& _source) override;
		Rml::TextureHandle GenerateTexture(Rml::Span<const unsigned char> _source, Rml::Vector2i _sourceDimensions) override;
		void ReleaseTexture(Rml::TextureHandle _texture) override;
		void EnableScissorRegion(bool _enable) override;
		void SetScissorRegion(Rml::Rectanglei _region) override;
		void EnableClipMask(bool _enable) override;
		void RenderToClipMask(Rml::ClipMaskOperation _operation, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation) override;
		void SetTransform(const Rml::Matrix4f* _transform) override;
		Rml::LayerHandle PushLayer() override;
		void CompositeLayers(Rml::LayerHandle _source, Rml::LayerHandle _destination, Rml::BlendMode _blendMode, Rml::Span<const Rml::CompiledFilterHandle> _filters) override;
		void PopLayer() override;
		Rml::TextureHandle SaveLayerAsTexture() override;
		Rml::CompiledFilterHandle SaveLayerAsMaskImage() override;
		Rml::CompiledFilterHandle CompileFilter(const Rml::String& _name, const Rml::Dictionary& _parameters) override;
		void ReleaseFilter(Rml::CompiledFilterHandle _filter) override;
		Rml::CompiledShaderHandle CompileShader(const Rml::String& _name, const Rml::Dictionary& _parameters) override;
		void RenderShader(Rml::CompiledShaderHandle _shader, Rml::CompiledGeometryHandle _geometry, Rml::Vector2f _translation, Rml::TextureHandle _texture) override;
		void ReleaseShader(Rml::CompiledShaderHandle _shader) override;

		Rml::TextureHandle loadTexture(juce::Image& _image) override;

		void executeRenderFunctions();

		void finishFrame();

	private:
		void addRenderFunction(Func _func);

		Handle createDummyHandle()
		{
			std::lock_guard lock(m_mutex);
			return m_nextHandle++;
		}

		void addHandle(Handle _dummy, Handle _real)
		{
			std::lock_guard lock(m_mutex);
			m_handles.insert({_dummy, _real});
		}

		Handle getRealHandle(const Handle _dummy)
		{
			std::lock_guard lock(m_mutex);
			auto it = m_handles.find(_dummy);
			if (it != m_handles.end())
				return it->second;
			return InvalidHandle;
		}

		void removeHandle(const Handle _dummy)
		{
			std::lock_guard lock(m_mutex);
			m_handles.erase(_dummy);
		}

		template<typename T>
		std::vector<std::remove_const_t<T>> copySpan(const Rml::Span<T>& _span)
		{
			std::vector<std::remove_const_t<T>> vertices(_span.size());
			std::copy(_span.begin(), _span.end(), vertices.begin());
			return vertices;
		}

		Handle m_nextHandle = 1;
		Renderer& m_renderer;
		std::mutex m_mutex;
		std::mutex m_mutexRender;
		std::vector<Func> m_enqueuedFunctions;
		std::vector<Func> m_renderFunctions;
		std::vector<Func> m_renderFunctionsCopy;
		std::unordered_map<Handle, Handle> m_handles;

		std::stack<Handle> m_layerHandles;
	};
}
