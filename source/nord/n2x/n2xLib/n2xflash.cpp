#include "n2xflash.h"

#include "n2xtypes.h"
#include "synthLib/os.h"

namespace n2x
{
	Flash::Flash()
	{
		return;
		const auto filename = synthLib::findROM(g_flashSize, g_flashSize);

		if(!filename.empty())
		{
			std::vector<uint8_t> d;
			if(synthLib::readFile(d, filename))
			{
				setData(d);
			}
		}
	}
}
