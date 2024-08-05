#include "n2xrom.h"

#include <algorithm>

#include "synthLib/os.h"

namespace n2x
{
	Rom::Rom() : RomData()
	{
		constexpr uint8_t key[] = {'n', 'r', '2', 0, 'n', 'L', '2', 0};

		const auto it = std::search(data().begin(), data().end(), std::begin(key), std::end(key));
		if(it == data().end())
			invalidate();
	}
}
