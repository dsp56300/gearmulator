#include "mainWindow.h"

#include "installerDocument.h"

#include "pluginManagerLib/config.h"

#include "juceRmlUi/juceRmlComponent.h"

namespace pluginManager
{
	namespace
	{
		// The window the app opens with. The document is laid out at 900dp wide and scaled to
		// whatever the window is, so this is also how big everything in it starts out.
		constexpr int g_windowWidth = 1080;
		constexpr int g_windowHeight = 840;

		// And how small it may be made. The document does not reflow when the window shrinks, it is
		// scaled, so without a floor the window can be dragged down to a few hundred pixels with the
		// whole list still in it and none of it readable.
		//
		// The width is just under the 900dp the document is laid out at, so the text is never much
		// smaller than it was drawn. The height is what shows the header and about five rows.
		constexpr int g_minWidth = 800;
		constexpr int g_minHeight = 500;

		// Kept free around the window on a screen too small for the size above.
		constexpr int g_screenMargin = 24;
	}

	MainWindow::MainWindow(InstallerDocument& _document)
		: DocumentWindow(juce::String(g_appTitle) + " " + g_appVersion, juce::Colours::darkgrey, DocumentWindow::allButtons)
	{
		setUsingNativeTitleBar(true);
		setContentNonOwned(_document.getRmlComponent(), true);
		setResizable(true, false);
		setResizeLimits(g_minWidth, g_minHeight, 32768, 32768);
		centreWithSize(g_windowWidth, g_windowHeight);
		setVisible(true);

		// Inside the part of the screen the taskbar or the menu bar leaves free, with a margin (EMU-249).
		// At 125% on a 1080p display that is about 816 points high, less than the window, and the taskbar
		// covered its bottom. The title bar and borders are outside these bounds with a native title bar,
		// and their size is only known once there is a window.
		const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(getScreenBounds());

		if (!display)
			return;

		const auto frame = getPeer() ? getPeer()->getFrameSizeIfPresent() : juce::ComponentPeer::OptionalBorderSize();
		auto room = display->userArea.reduced(g_screenMargin);

		if (frame)
			room = (*frame).subtractedFrom(room);

		const auto width = std::min(g_windowWidth, room.getWidth());
		const auto height = std::min(g_windowHeight, room.getHeight());

		setResizeLimits(std::min(g_minWidth, width), std::min(g_minHeight, height), 32768, 32768);
		setBounds(room.withSizeKeepingCentre(width, height));
	}

	MainWindow::~MainWindow() = default;

	void MainWindow::closeButtonPressed()
	{
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}

	// Only what the document did not want. RmlUi is asked first, and it answers for a paste into a
	// text field - so Ctrl+V in the login box still pastes into the login box, and only a Ctrl+V
	// that nothing else claimed arrives here.
	bool MainWindow::keyPressed(const juce::KeyPress& _key)
	{
		if (m_onText && _key == juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0))
		{
			m_onText(juce::SystemClipboard::getTextFromClipboard().toStdString());
			return true;
		}

		return DocumentWindow::keyPressed(_key);
	}
}
