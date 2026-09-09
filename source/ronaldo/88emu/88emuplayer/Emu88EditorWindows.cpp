#include "Emu88Editor.h"
#include "Emu88EditorBindings.h"
#include "Emu88EditorWindows.h"

namespace emu88Player
{
	using namespace editor;

	void Editor::showKeyboard()
	{
		if(m_keyboardWindow)
		{
			m_keyboardWindow->toFront(true);
			return;
		}
		m_keyboardWindow = std::make_unique<KeyboardWindow>(*this, m_processor.getName().toStdString(),
		                                                    buildKeyboardGroups());
		m_keyboardWindow->setVisible(true);
	}

	void Editor::showAbout()
	{
		if(m_aboutWindow)
		{
			m_aboutWindow->toFront(true);
			return;
		}
		m_aboutWindow = std::make_unique<AboutWindow>(*this, m_processor.getName().toStdString());
		m_aboutWindow->setVisible(true);
	}

	void KeyboardWindow::closeButtonPressed()
	{
		const juce::WeakReference<Editor> safeThis(&m_owner);
		juce::MessageManager::callAsync([safeThis]
		{
			if(auto* editor = safeThis.get())
				editor->m_keyboardWindow.reset();
		});
	}

	void AboutWindow::closeButtonPressed()
	{
		const juce::WeakReference<Editor> safeThis(&m_owner);
		juce::MessageManager::callAsync([safeThis]
		{
			if(auto* editor = safeThis.get())
				editor->m_aboutWindow.reset();
		});
	}
}
