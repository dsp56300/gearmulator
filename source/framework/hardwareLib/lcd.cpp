#include "lcd.h"

namespace hwLib
{
	LCD::LCD()
	{
		// This panel is wired as two lines, and the firmware it serves addresses the second one at
		// 0x40. Say so up front rather than waiting for a Function set: the old implementation had
		// the 0x40 mapping baked in, so anything that worked before must keep working from the
		// first byte, and real firmware sends its own Function set anyway.
		m_hd.write(false, 0x38);

		// The old implementation scrolled the display by moving characters whenever the firmware
		// wrote past the end of a line. That is not what the chip does - it wraps the address
		// counter - but the panels this serves rely on it, so use the shim that reproduces it.
		m_hd.setAutoFollow(true);

		m_hd.setChangeCallback([this]
		{
			m_hd.copyVisibleDdRam(m_ddRam.data());

			if (m_changeCallback)
				m_changeCallback();
		});

		m_hd.copyVisibleDdRam(m_ddRam.data());
	}
}
