#pragma once

#include "synthLib/midiTypes.h"

#include <string>

namespace genericVirusUI
{
	class VirusPatchFileParser
	{
	public:
		static bool parse(synthLib::SysexBufferList& _results, const synthLib::SysexBuffer& _data, const std::string& _filename);

	private:
		static void mergeArrangements(synthLib::SysexBufferList& _results);
	};
}
