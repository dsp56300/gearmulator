#include "n2xEditor.h"

#include "BinaryData.h"
#include "n2xPluginProcessor.h"

#include "n2xController.h"
#include "n2xPatchManager.h"

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
	}

	Editor::~Editor()
	{
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
}
