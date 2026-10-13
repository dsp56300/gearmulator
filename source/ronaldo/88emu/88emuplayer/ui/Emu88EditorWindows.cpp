#include "88emuplayer/ui/Emu88Editor.h"
#include "88emuplayer/ui/Emu88EditorBindings.h"
#include "88emuplayer/ui/Emu88EditorWindows.h"
#include "88emuplayer/ui/Emu88PartEditor.h"

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

	void Editor::showPartEditor(const bool _show)
	{
		if(!_show)
		{
			if(!m_partEditorWindow)
				return;
			// The window may be closing from one of its own handlers, so it goes once they returned
			m_partEditorWindow->setVisible(false);
			const juce::WeakReference<Editor> safeThis(this);
			juce::MessageManager::callAsync([safeThis]
			{
				if(auto* editor = safeThis.get())
					if(editor->m_partEditorWindow && !editor->m_partEditorWindow->isVisible())
						editor->m_partEditorWindow.reset();
			});
			return;
		}
		if(m_partEditorWindow)
		{
			m_partEditorWindow->setVisible(true);
			m_partEditorWindow->toFront(true);
			return;
		}
		m_partEditorWindow = std::make_unique<PartEditorWindow>(*this, m_processor);
		m_partEditorWindow->setVisible(true);
	}

	void RomRequirementsWindow::closeButtonPressed()
	{
		const juce::WeakReference<Editor> safeThis(&m_owner);
		juce::MessageManager::callAsync([safeThis]
		{
			if(auto* editor = safeThis.get())
				editor->m_romWindow.reset();
		});
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
