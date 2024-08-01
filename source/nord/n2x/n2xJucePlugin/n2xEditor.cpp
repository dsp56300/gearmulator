#include "n2xEditor.h"

#include "BinaryData.h"

#include "n2xArp.h"
#include "n2xController.h"
#include "n2xLcd.h"
#include "n2xPart.h"
#include "n2xParts.h"
#include "n2xPatchManager.h"
#include "n2xPluginProcessor.h"

#include "jucePluginLib/parameterbinding.h"
#include "jucePluginLib/pluginVersion.h"

#include "n2xLib/n2xdevice.h"

namespace n2xJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename)
	: jucePluginEditorLib::Editor(_processor, _binding, std::move(_skinFolder))
	, m_controller(dynamic_cast<Controller&>(_processor.getController()))
	, m_parameterBinding(_binding)
	{
		create(_jsonFilename);

		addMouseListener(this, true);
		{
			// Init Patch Manager
			const auto container = findComponent("ContainerPatchManager");
			constexpr auto scale = 1.0f;
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

		if(auto* versionNumber = findComponentT<juce::Label>("VersionNumber", false))
		{
			versionNumber->setText(pluginLib::Version::getVersionString(), juce::dontSendNotification);
		}

		if(auto* romSelector = findComponentT<juce::ComboBox>("RomSelector"))
		{
			const auto* dev = dynamic_cast<const n2x::Device*>(getProcessor().getPlugin().getDevice());

			if(dev != nullptr && getProcessor().isPluginValid())
			{
				constexpr int id = 1;

				const auto name = juce::File(dev->getRomFilename()).getFileName();
				romSelector->addItem(name, id);
			}
			else
			{
				romSelector->addItem("<No ROM found>", 1);
			}
			romSelector->setSelectedId(1);
			romSelector->setInterceptsMouseClicks(false, false);
		}

		m_arp.reset(new Arp(*this));
		m_lcd.reset(new Lcd(*this));
		m_parts.reset(new Parts(*this));

		onPartChanged.set(m_controller.onCurrentPartChanged, [this](const uint8_t& _part)
		{
			setCurrentPart(_part);
			m_parameterBinding.setPart(_part);
		});
	}

	Editor::~Editor()
	{
		m_arp.reset();
		m_lcd.reset();
		m_parts.reset();
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
		return {};
	}

	genericUI::Button<juce::DrawableButton>* Editor::createJuceComponent(genericUI::Button<juce::DrawableButton>* _button, genericUI::UiObject& _object, const std::string& _name, const juce::DrawableButton::ButtonStyle _buttonStyle)
	{
		if(_name == "PerfMidiChannelA" || _name == "PerfMidiChannelB" || _name == "PerfMidiChannelC" || _name == "PerfMidiChannelD")
			return new Part(*this, _name, _buttonStyle);

		return jucePluginEditorLib::Editor::createJuceComponent(_button, _object, _name, _buttonStyle);
	}
}
