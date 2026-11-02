#pragma once

#ifdef __APPLE__

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace juce
{
	class Component;
}

namespace juceRmlUi
{
	class MetalContext
	{
	public:
		class Listener
		{
		public:
			virtual ~Listener() = default;
			virtual void metalContextCreated(MetalContext&) = 0;
			virtual void renderMetal(MetalContext&) = 0;
			virtual void metalContextClosing(MetalContext&) = 0;
			// The view changed size, on the message thread, inside the Core Animation transaction that
			// shows it. See renderSynchronously().
			virtual void metalViewResized(MetalContext&) {}
		};

		MetalContext();
		~MetalContext();

		MetalContext(const MetalContext&) = delete;
		MetalContext(MetalContext&&) = delete;
		MetalContext& operator=(const MetalContext&) = delete;
		MetalContext& operator=(MetalContext&&) = delete;

		void setListener(Listener* _listener);
		void attachTo(juce::Component& _component);
		void detach();
		void triggerRepaint();
		void setContinuousRepainting(bool _enabled);
		// Reads the view geometry and resizes the layer to match. Message thread only, the render
		// thread just consumes the result.
		void updateDrawableSize();
		// Draws a frame on the message thread and presents it with the current Core Animation
		// transaction, so it reaches the screen together with a new view size instead of a frame or
		// more after it. _prepareFrame produces the frame and returns whether there is one. Returns
		// false if nothing was drawn, the render thread then draws the next frame as usual.
		bool renderSynchronously(const std::function<bool()>& _prepareFrame);
		// Called by the view when its size changes, see metalViewResized()
		void viewResized();

		double getRenderingScale() const;
		int getViewportWidth() const;
		int getViewportHeight() const;

		// Returns id<MTLDevice> as void* for C++ compatibility
		void* getDevice() const;
		// Returns id<CAMetalDrawable> as void*, caller must bridge-cast in ObjC++ code
		void* nextDrawable();
		// Returns MTLPixelFormat as uint
		unsigned long getPixelFormat() const;

	private:
		void renderLoop();
		void createMetalLayer();
		void destroyMetalLayer();

		void* m_device = nullptr;
		void* m_commandQueue = nullptr;
		void* m_metalLayer = nullptr;
		void* m_metalView = nullptr;
		void* m_viewAttachment = nullptr; // juce::ReferenceCountedObject* via bridging

		Listener* m_listener = nullptr;
		juce::Component* m_component = nullptr;

		std::unique_ptr<std::thread> m_renderThread;
		std::atomic<bool> m_shouldExit{false};
		std::atomic<bool> m_repaintRequested{false};
		std::atomic<bool> m_continuousRepainting{false};
		std::condition_variable m_renderCV;
		std::mutex m_renderMutex;
		// held while a frame is drawn, by the render thread or by renderSynchronously()
		std::timed_mutex m_frameMutex;

		std::atomic<int> m_viewportWidth{0};
		std::atomic<int> m_viewportHeight{0};
		std::atomic<double> m_renderingScale{1.0};

		bool m_attached = false;
		std::atomic<bool> m_contextCreated{false};
	};
}

#endif // __APPLE__
