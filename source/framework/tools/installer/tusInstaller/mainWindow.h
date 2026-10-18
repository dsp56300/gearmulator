#pragma once

#include <functional>
#include <string>

#include <JuceHeader.h>

namespace juceRmlUi { class RmlComponent; }

namespace pluginManager
{
	class InstallerDocument;

	class MainWindow : public juce::DocumentWindow
	{
	public:
		explicit MainWindow(InstallerDocument& _document);
		~MainWindow() override;

		void closeButtonPressed() override;

		// Text that was pasted into the window. The window knows only that something arrived; what
		// it might be is somebody else's question. A file dropped on the window arrives the same
		// way, but through the Rml component, which is what JUCE hands every file drag to.
		using TextHandler = std::function<void(const std::string&)>;

		void setTextHandler(TextHandler _handler) { m_onText = std::move(_handler); }

		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		TextHandler m_onText;

		JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
	};
}
