#include "rom.h"

#include "../synthLib/os.h"

namespace mqLib
{
	ROM::ROM(const std::string& _filename)
	{
		FILE* hFile = fopen(_filename.c_str(), "rb");
		if(!hFile)
			return;

		fseek(hFile, 0, SEEK_END);
		const auto size = ftell(hFile);
		fseek(hFile, 0, SEEK_SET);

		m_data.resize(size);
		const auto numRead = fread(&m_data[0], 1, size, hFile);
		fclose(hFile);

		if(numRead != static_cast<size_t>(size))
			return;

		fclose(hFile);
	}
}
