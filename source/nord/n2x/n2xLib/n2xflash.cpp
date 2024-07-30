#include "n2xflash.h"

#include "n2xhardware.h"
#include "n2xtypes.h"
#include "synthLib/os.h"

namespace n2x
{
	Flash::Flash(Hardware& _hardware) : m_hardware(_hardware)
	{
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

	uint8_t Flash::onReadByte()
	{
		if(getAddress() == 0x31 && --m_bootCounter == 0)
			m_hardware.notifyBootFinished();

		return I2cFlash::onReadByte();
	}
}
