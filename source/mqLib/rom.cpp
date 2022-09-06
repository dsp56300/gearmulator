#include "rom.h"

#include "../synthLib/os.h"

namespace mqLib
{
	bool ROM::loadFromFile(const std::string& _filename)
	{
		FILE* hFile = fopen(_filename.c_str(), "rb");
		if(!hFile)
			return false;

		fseek(hFile, 0, SEEK_END);
		const auto size = ftell(hFile);
		fseek(hFile, 0, SEEK_SET);

		m_buffer.resize(size);
		const auto numRead = fread(&m_buffer[0], 1, size, hFile);
		fclose(hFile);

		if(numRead != static_cast<size_t>(size))
			m_buffer.clear();

		if(numRead != getSize())
			m_buffer.clear();

		if(!m_buffer.empty())
		{
			m_data = &m_buffer[0];
			return true;
		}
		return false;
	}
}
