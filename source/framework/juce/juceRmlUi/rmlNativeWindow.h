#pragma once

namespace juceRmlUi
{
	// Windows: the native window erases its background black. Windows erases a window as it shows it, the first paint
	// only follows once the message queue is empty, and juce does not erase at all, so until then the window showed
	// the host's background (BUG-10559). Does nothing on other platforms
	void setBlackWindowBackground(void* _nativeWindowHandle);
}
