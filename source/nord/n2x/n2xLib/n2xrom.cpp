#include "n2xrom.h"

#include <algorithm>

#include "synthLib/os.h"

namespace n2x
{
	Rom::Rom() : RomData()
	{
		if(!isValidRom(data()))
			invalidate();
	}

	Rom::Rom(const std::string& _filename) : RomData(_filename)
	{
		if(!isValidRom(data()))
			invalidate();
	}

	bool Rom::isValidRom(const std::vector<uint8_t>& _data)
	{
		constexpr uint8_t key[] = {'n', 'r', '2', 0, 'n', 'L', '2', 0};

		if(_data.size() < std::size(key))
			return false;

		const auto it = std::search(_data.begin(), _data.end(), std::begin(key), std::end(key));
		return it != _data.end();
	}
}
