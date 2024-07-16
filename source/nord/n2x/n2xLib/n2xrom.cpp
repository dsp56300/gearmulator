#include "n2xrom.h"

#include "synthLib/os.h"

namespace n2x
{
	Rom::Rom()
	{
		const auto filename = synthLib::findROM(m_data.size(), m_data.size());
		if(filename.empty())
			return;
		std::vector<uint8_t> data;
		if(!synthLib::readFile(data, filename))
			return;
		if(data.size() != m_data.size())
			return;
		std::copy(data.begin(), data.end(), m_data.begin());
		m_filename = filename;
	}
}
