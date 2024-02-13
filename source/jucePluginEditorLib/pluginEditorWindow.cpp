#include "pluginEditorWindow.h"
#include "pluginEditorState.h"

#include "patchmanager/patchmanager.h"

namespace jucePluginEditorLib
{

//==============================================================================
EditorWindow::EditorWindow(juce::AudioProcessor& _p, PluginEditorState& _s, juce::PropertiesFile& _config)
	: AudioProcessorEditor(&_p), m_state(_s), m_config(_config)
{
	addMouseListener(this, true);

	m_state.evSkinLoaded = [&](juce::Component* _component)
	{
		setUiRoot(_component);
	};

	m_state.evSetGuiScale = [&](const int _scale)
	{
		if(getNumChildComponents() > 0)
			setGuiScale(getChildComponent(0), _scale);
	};

	m_state.enableBindings();

	setUiRoot(m_state.getUiRoot());
}

EditorWindow::~EditorWindow()
{
	m_state.evSetGuiScale = [&](int){};
	m_state.evSkinLoaded = [&](juce::Component*){};

	m_state.disableBindings();

	setUiRoot(nullptr);
}

void EditorWindow::setGuiScale(juce::Component* _comp, int percent)
{
	if(!_comp)
		return;

	const auto s = static_cast<float>(percent)/100.0f * m_state.getRootScale();
	_comp->setTransform(juce::AffineTransform::scale(s,s));

	setSize(static_cast<int>(m_state.getWidth() * s), static_cast<int>(m_state.getHeight() * s));

	m_config.setValue("scale", percent);
	m_config.saveIfNeeded();
}

void EditorWindow::setUiRoot(juce::Component* _component)
{
	removeAllChildren();

	if(!_component)
		return;

    const auto scale = m_config.getIntValue("scale", 100);

	setGuiScale(_component, scale);
	addAndMakeVisible(_component);
}

void EditorWindow::mouseDown(const juce::MouseEvent& event)
{
	if(!event.mods.isPopupMenu())
	{
		AudioProcessorEditor::mouseDown(event);
		return;
	}

	if(event.eventComponent)
	{
		// file browsers have their own menu, do not display two menus at once
		if(event.eventComponent->findParentComponentOfClass<juce::FileBrowserComponent>())
			return;

		// patch manager has its own context menu, too
		if (event.eventComponent->findParentComponentOfClass<patchManager::PatchManager>())
			return;
	}

	if(dynamic_cast<juce::TextEditor*>(event.eventComponent))
		return;
	if(dynamic_cast<juce::Button*>(event.eventComponent))
		return;

	m_state.openMenu();
}

}