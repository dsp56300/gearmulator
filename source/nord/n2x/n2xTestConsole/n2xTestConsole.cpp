#include <iostream>

#include "n2xLib/n2xhardware.h"
#include "n2xLib/n2xrom.h"

namespace n2x
{
	class Hardware;
}

int main()
{
	const n2x::Rom rom;

	if(!rom.isValid())
	{
		std::cout << "Failed to load rom file\n";
		return -1;
	}

	std::unique_ptr<n2x::Hardware> hw;
	hw.reset(new n2x::Hardware());

	while(true)
		hw->process();
}
