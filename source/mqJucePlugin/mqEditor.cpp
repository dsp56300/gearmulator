#include "mqEditor.h"

#include "BinaryData.h"

namespace mqJucePlugin
{
	Editor::Editor() : genericUI::Editor(static_cast<EditorInterface&>(*this))
	{
	}

	const char* Editor::findNamedResourceByFilename(const std::string& _filename, uint32_t& _size)
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
}
