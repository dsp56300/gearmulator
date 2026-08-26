#include "serialization.h"

namespace pluginLib::patchDB
{
	std::vector<std::string> Serialization::split(const std::string& _string, const char _delim)
	{
		std::vector<std::string> elems;

		size_t off = 0;

		while (off < _string.size())
		{
			auto idx = _string.find(_delim, off);

			if (idx == std::string::npos)
				idx = _string.size();

			elems.push_back(_string.substr(off, idx - off));
			off = idx + 1;
		}

		return elems;
	}
}
