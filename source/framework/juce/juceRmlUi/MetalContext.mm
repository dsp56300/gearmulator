#ifdef __APPLE__

#include "MetalContext.h"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>

#include "juce_gui_basics/juce_gui_basics.h"
#include "juce_gui_extra/juce_gui_extra.h"

// Bridging helpers for void* <-> ObjC id stored in header
#define MTL_DEVICE       ((__bridge id<MTLDevice>)m_device)
#define MTL_CMD_QUEUE    ((__bridge id<MTLCommandQueue>)m_commandQueue)
#define MTL_LAYER        ((__bridge CAMetalLayer*)m_metalLayer)

namespace juceRmlUi
{
	MetalContext::MetalContext()
	{
		id<MTLDevice> device = MTLCreateSystemDefaultDevice();
		m_device = (void*)[device retain];
		if (m_device)
		{
			id<MTLCommandQueue> queue = [MTL_DEVICE newCommandQueue];
			m_commandQueue = (void*)queue; // newCommandQueue returns +1 retained
		}
	}

	MetalContext::~MetalContext()
	{
		detach();

		if (m_commandQueue) { [(id)m_commandQueue release]; m_commandQueue = nullptr; }
		if (m_device) { [(id)m_device release]; m_device = nullptr; }
	}

	void MetalContext::setListener(Listener* _listener)
	{
		m_listener = _listener;
	}

	void MetalContext::attachTo(juce::Component& _component)
	{
		if (m_attached)
			return; // Already attached, don't re-attach

		m_component = &_component;

		if (!m_device)
		{
			NSLog(@"MetalContext::attachTo: no device");
			return;
		}

		createMetalLayer();

		if (!m_attached)
		{
			NSLog(@"MetalContext::attachTo: createMetalLayer failed, peer=%p",
				_component.getPeer() ? _component.getPeer()->getNativeHandle() : nullptr);
			return;
		}

		NSLog(@"MetalContext::attachTo: success, starting render thread");
		m_shouldExit = false;
		m_renderThread = std::make_unique<std::thread>([this] { renderLoop(); });
	}

	void MetalContext::detach()
	{
		if (!m_attached)
			return;

		m_shouldExit = true;
		m_renderCV.notify_all();

		if (m_renderThread && m_renderThread->joinable())
			m_renderThread->join();

		m_renderThread.reset();

		destroyMetalLayer();

		m_component = nullptr;
		m_attached = false;
	}

	void MetalContext::triggerRepaint()
	{
		m_repaintRequested = true;
		m_renderCV.notify_one();
	}

	void MetalContext::setContinuousRepainting(bool _enabled)
	{
		m_continuousRepainting = _enabled;
		if (_enabled)
			m_renderCV.notify_one();
	}

	double MetalContext::getRenderingScale() const
	{
		return m_renderingScale;
	}

	int MetalContext::getViewportWidth() const
	{
		return m_viewportWidth;
	}

	int MetalContext::getViewportHeight() const
	{
		return m_viewportHeight;
	}

	void* MetalContext::getDevice() const
	{
		return m_device;
	}

	void* MetalContext::nextDrawable()
	{
		if (!m_metalLayer)
			return nullptr;

		id<CAMetalDrawable> drawable = [MTL_LAYER nextDrawable];
		return (__bridge void*)drawable;
	}

	unsigned long MetalContext::getPixelFormat() const
	{
		if (m_metalLayer)
			return MTL_LAYER.pixelFormat;

		return MTLPixelFormatBGRA8Unorm;
	}

	void MetalContext::renderLoop()
	{
		@autoreleasepool
		{
			if (m_listener)
				m_listener->metalContextCreated(*this);

			m_contextCreated = true;
		}

		while (!m_shouldExit)
		{
			{
				std::unique_lock<std::mutex> lock(m_renderMutex);
				m_renderCV.wait_for(lock, std::chrono::milliseconds(16), [this]
				{
					return m_shouldExit.load() || m_repaintRequested.load() || m_continuousRepainting.load();
				});
			}

			if (m_shouldExit)
				break;

			m_repaintRequested = false;

			@autoreleasepool
			{
				updateDrawableSize();

				if (m_listener && m_viewportWidth > 0 && m_viewportHeight > 0)
					m_listener->renderMetal(*this);
			}
		}

		@autoreleasepool
		{
			if (m_listener && m_contextCreated)
				m_listener->metalContextClosing(*this);

			m_contextCreated = false;
		}
	}

	void MetalContext::createMetalLayer()
	{
		if (!m_component || !m_device)
			return;

		auto* peer = m_component->getPeer();
		if (!peer)
			return;

		NSView* nativeView = (__bridge NSView*)peer->getNativeHandle();
		if (!nativeView)
			return;

		CAMetalLayer* layer = [CAMetalLayer layer];
		layer.device = MTL_DEVICE;
		layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
		layer.framebufferOnly = NO; // We need to read back for screenshots
		layer.opaque = YES;

		// Create an NSView backed by the Metal layer.
		NSView* metalView = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 100, 100)];
		metalView.wantsLayer = YES;
		metalView.layer = layer;

		// Use JUCE's NSViewComponent::attachViewToComponent to embed the view.
		// This handles plugin host view hierarchies correctly (AU, VST3, etc.),
		// including peer changes when the editor is closed and reopened.
		// attachViewToComponent returns a ref-counted object that manages the view lifecycle
		auto* attachment = juce::NSViewComponent::attachViewToComponent(*m_component, metalView);
		if (attachment) attachment->incReferenceCount();
		m_viewAttachment = attachment;

		m_metalView = (void*)[metalView retain];
		m_metalLayer = (void*)[layer retain];

		m_attached = true;

		updateDrawableSize();
	}

	void MetalContext::destroyMetalLayer()
	{
		if (m_viewAttachment)
		{
			auto* attachment = static_cast<juce::ReferenceCountedObject*>(m_viewAttachment);
			attachment->decReferenceCount();
			m_viewAttachment = nullptr;
		}
		if (m_metalView)
		{
			[(id)m_metalView release];
			m_metalView = nullptr;
		}
		if (m_metalLayer)
		{
			[(id)m_metalLayer release];
			m_metalLayer = nullptr;
		}
	}

	void MetalContext::updateDrawableSize()
	{
		if (!m_metalLayer || !m_metalView)
			return;

		NSView* metalView = (__bridge NSView*)m_metalView;
		NSWindow* window = metalView.window;
		if (!window)
			return;

		CAMetalLayer* layer = MTL_LAYER;

		const auto scale = window.backingScaleFactor;
		m_renderingScale = scale;
		layer.contentsScale = scale;

		// The NSView auto-resizes via autoresizingMask. Just sync the
		// layer frame to the view bounds and update the drawable size.
		const CGRect viewBounds = metalView.bounds;

		if (!CGRectEqualToRect(layer.frame, viewBounds))
		{
			[CATransaction begin];
			[CATransaction setDisableActions:YES];
			layer.frame = viewBounds;
			[CATransaction commit];
		}

		const auto drawableWidth = static_cast<int>(viewBounds.size.width * scale);
		const auto drawableHeight = static_cast<int>(viewBounds.size.height * scale);

		if (drawableWidth != m_viewportWidth || drawableHeight != m_viewportHeight)
		{
			layer.drawableSize = CGSizeMake(drawableWidth, drawableHeight);
			m_viewportWidth = drawableWidth;
			m_viewportHeight = drawableHeight;
		}
	}
}

#endif // __APPLE__
