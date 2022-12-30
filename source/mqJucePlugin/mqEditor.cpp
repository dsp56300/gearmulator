#include "mqEditor.h"

#include "BinaryData.h"
#include "PluginProcessor.h"

#include "mqController.h"

#include "../jucePluginLib/parameterbinding.h"

namespace mqJucePlugin
{
	Editor::Editor(jucePluginEditorLib::Processor& _processor, pluginLib::ParameterBinding& _binding, std::string _skinFolder, const std::string& _jsonFilename)
	: jucePluginEditorLib::Editor(_processor, _binding, std::move(_skinFolder))
	{
		create(_jsonFilename);

		m_frontPanel.reset(new FrontPanel(*this, static_cast<Controller&>(_processor.getController())));

		if(findComponent("ContainerPatchList", false))
			m_patchBrowser.reset(new PatchBrowser(*this, _processor.getController(), _processor.getConfig()));
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
}
