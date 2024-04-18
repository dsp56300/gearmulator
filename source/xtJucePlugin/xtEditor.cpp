#include "xtEditor.h"

#include "BinaryData.h"
#include "PluginProcessor.h"

#include "xtController.h"
#include "xtFocusedParameter.h"
#include "xtFrontPanel.h"
#include "xtPatchManager.h"
#include "../jucePluginLib/parameterbinding.h"

namespace xtJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename)
	: jucePluginEditorLib::Editor(_processor, _binding, std::move(_skinFolder))
	, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	, m_parameterBinding(_binding)
	, m_playModeChangeListener(m_controller.onPlayModeChanged)
	{
		create(_jsonFilename);

		m_focusedParameter.reset(new FocusedParameter(m_controller, _binding, *this));

		m_frontPanel.reset(new FrontPanel(*this, m_controller));

		m_parts.reset(new Parts(*this));

		addMouseListener(this, true);

		{
			// Init Patch Manager
			const auto container = findComponent("ContainerPatchManager");
			constexpr auto scale = 1.3f;
			const float x = static_cast<float>(container->getX());
			const float y = static_cast<float>(container->getY());
			const float w = static_cast<float>(container->getWidth());
			const float h = static_cast<float>(container->getHeight());
			container->setTransform(juce::AffineTransform::scale(scale, scale));
			container->setSize(static_cast<int>(w / scale),static_cast<int>(h / scale));
			container->setTopLeftPosition(static_cast<int>(x / scale),static_cast<int>(y / scale));

			const auto configOptions = getProcessor().getConfigOptions();
			const auto dir = configOptions.getDefaultFile().getParentDirectory();

			setPatchManager(new PatchManager(*this, container, dir));
		}

		m_btMultiMode = findComponentT<juce::Button>("MultiModeButton");
		m_ledMultiMode = findComponentT<juce::Button>("MultiModeLED");

		m_btMultiMode->onClick = [this]
		{
			m_controller.setPlayMode(m_btMultiMode->getToggleState());
		};

		m_playModeChangeListener = [this](const bool& isMultiMode)
		{
			m_btMultiMode->setToggleState(isMultiMode, juce::dontSendNotification);
			m_ledMultiMode->setToggleState(isMultiMode, juce::dontSendNotification);
		};

		m_btSave = findComponentT<juce::DrawableButton>("SaveButton");
		m_btSave->onClick = [this]
		{
			juce::PopupMenu menu;

			const auto countAdded = getPatchManager()->createSaveMenuEntries(menu, getPatchManager()->getCurrentPart());

			if(countAdded)
				menu.showMenuAsync(juce::PopupMenu::Options());
		};
	}

	Editor::~Editor()
	{
		m_frontPanel.reset();
	}

	const char* Editor::findEmbeddedResource(const std::string& _filename, uint32_t& _size)
	{
		for(size_t i=0; i<BinaryData::namedResourceListSize; ++i)
		{
			if (BinaryData::originalFilenames[i] != _filename)
				continue;

			int size = 0;
			const auto res = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
			_size = static_cast<uint32_t>(size);
			return res;
		}
		return nullptr;
	}

	const char* Editor::findResourceByFilename(const std::string& _filename, uint32_t& _size)
	{
		return findEmbeddedResource(_filename, _size);
	}

	std::pair<std::string, std::string> Editor::getDemoRestrictionText() const
	{
		return {"Vavra",
			"Xenia runs in demo mode\n"
			"\n"
			"The following features are disabled:\n"
			"- Saving/Exporting Presets\n"
			"- Plugin state is not preserve"
		};
	}

	XtLcd* Editor::getLcd() const
	{
		return m_frontPanel->getLcd();
	}

	Parts& Editor::getParts() const
	{
		assert(m_parts);
		return *m_parts;
	}

	genericUI::Button<juce::DrawableButton>* Editor::createJuceComponent(
		genericUI::Button<juce::DrawableButton>* _button, genericUI::UiObject& _object, const std::string& _name,
		juce::DrawableButton::ButtonStyle _buttonStyle)
	{
		if(_name == "PartButtonSmall")
			return new PartButton(*this, _name, _buttonStyle);

		return jucePluginEditorLib::Editor::createJuceComponent(_button, _object, _name, _buttonStyle);
	}

	void Editor::setCurrentPart(const uint8_t _part)
	{
		m_controller.setCurrentPart(_part);
		m_parameterBinding.setPart(_part);

		jucePluginEditorLib::Editor::setCurrentPart(_part);
	}

	void Editor::mouseEnter(const juce::MouseEvent& _event)
	{
		m_focusedParameter->onMouseEnter(_event);
	}
}
