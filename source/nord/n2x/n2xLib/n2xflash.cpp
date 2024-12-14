#include "n2xflash.h"

#include "n2xhardware.h"
#include "n2xtypes.h"

#include "baseLib/filesystem.h"

#include "synthLib/os.h"

namespace n2x
{
	Flash::Flash(Hardware& _hardware) : m_hardware(_hardware)
	{
		const auto filename = synthLib::findROM(g_flashSize, g_flashSize);

		if(!filename.empty())
		{
			std::vector<uint8_t> d;
			if(baseLib::filesystem::readFile(d, filename))
			{
				setData(d);
			}
		}
	}
}
