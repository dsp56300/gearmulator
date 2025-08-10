#pragma once

#include <mutex>
#include <variant>

#include "RmlUi/Core/RenderInterface.h"

namespace juce
{
	class Image;
}

namespace juceRmlUi
{
	class DataProvider;

	class RendererProxy : public Rml::RenderInterface
	{
	public:
		using Func = std::function<void()>;
		using Handle = uintptr_t;
		static constexpr Handle InvalidHandle = 0;

		enum HandleType
		{
			HandleTypeCompiledGeometry,
			HandleTypeTexture,
			HandleTypeCompiledFilter,
			HandleTypeCompiledShader,
			HandleTypeLayer
		};

		// we need strongly typed handles to avoid confusion between different types of handles, they all have the same underlying type
		template<typename T, HandleType HT>
		struct StrongHandle
		{
			using Type = T;
			static constexpr HandleType MyHandleType = HT;

			explicit StrongHandle(const Handle _handle) : handle(_handle) {}

			StrongHandle(StrongHandle&&) noexcept = default;
			StrongHandle(const StrongHandle&) = default;

			Handle handle;
		};

		using HandleCompiledGeometry = StrongHandle<Rml::CompiledGeometryHandle, HandleTypeCompiledGeometry>;
		using HandleTexture = StrongHandle<Rml::TextureHandle, HandleTypeTexture>;
		using HandleCompiledFilter = StrongHandle<Rml::CompiledFilterHandle, HandleTypeCompiledFilter>;
		using HandleCompiledShader = StrongHandle<Rml::CompiledShaderHandle, HandleTypeCompiledShader>;
		using HandleLayer = StrongHandle<Rml::LayerHandle, HandleTypeLayer>;

		using HandleVariant = std::variant<HandleCompiledGeometry, HandleTexture, HandleCompiledFilter, HandleCompiledShader, HandleLayer>;

		explicit RendererProxy(Rml::CoreInstance& _coreInstance, DataProvider& _dataProvider);
		~RendererProxy() override;

		// Rml::RenderInterface overrides
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

		void executeRenderFunctions();

		void finishFrame();

		void setRenderer(Rml::RenderInterface* _renderer);

	private:
		bool loadImage(juce::Image& _image, Rml::Vector2i& _textureDimensions, const Rml::String& _source) const;

		DataProvider& getDataProvider() const { return m_dataProvider; }

		Rml::TextureHandle loadTexture(juce::Image& _image);
		void addRenderFunction(Func _func);

		Handle createDummyHandle()
		{
			std::lock_guard lock(m_mutex);
			return m_nextHandle++;
		}

		template<typename T>
		void addHandle(Handle _dummy, Handle _real)
		{
			T h(_real);
			HandleVariant v(h);
			std::lock_guard lock(m_mutex);
			m_handles.insert({_dummy, v});
		}

		template<typename T>
		Handle getRealHandle(const Handle _dummy)
		{
			std::lock_guard lock(m_mutex);
			auto it = m_handles.find(_dummy);
			if (it != m_handles.end())
			{
				T* strongHandle = std::get_if<T>(&it->second);
				if (!strongHandle)
					return InvalidHandle;
				return strongHandle->handle;
			}
			return InvalidHandle;
		}

		void removeHandle(const Handle _dummy)
		{
			std::lock_guard lock(m_mutex);
			m_handles.erase(_dummy);
		}

		bool exists(const Handle _dummy)
		{
			std::lock_guard lock(m_mutex);
			return m_handles.find(_dummy) != m_handles.end();
		}

		template<typename T>
		std::vector<std::remove_const_t<T>> copySpan(const Rml::Span<T>& _span)
		{
			std::vector<std::remove_const_t<T>> vertices(_span.size());
			std::copy(_span.begin(), _span.end(), vertices.begin());
			return vertices;
		}

		DataProvider& m_dataProvider;

		Handle m_nextHandle = 1;
		Rml::RenderInterface* m_renderer = nullptr;
		std::mutex m_mutex;
		std::mutex m_mutexRender;
		std::vector<Func> m_enqueuedFunctions;
		std::vector<std::vector<Func>> m_renderFunctions;
		std::vector<Func> m_renderFunctionsToExecute;
		std::unordered_map<Handle, HandleVariant> m_handles;

		std::stack<Handle> m_layerHandles;
	};
}
