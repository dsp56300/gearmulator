#pragma once

#include <cstdint>
#include <vector>

#include "baseLib/binarystream.h"

namespace synthLib
{
	class SounddiverLibLoader
	{
	public:
		using Data = std::vector<uint8_t>;

		struct Chunk
		{
			char fourCC[5];
			Data data;
		};

		struct ListEntry
		{
			// header
			uint8_t entryType;
			uint8_t modelTypeA, modelTypeB;	
			uint8_t unknown3;
			uint16_t year;
			uint8_t month;
			uint8_t day;
			uint8_t hour;
			uint8_t minute;
			uint8_t second;
			uint8_t unknown8;
			uint8_t unknown9;
			uint8_t deviceId;
			uint8_t unknown11;

			std::string name;
			std::string location;
			std::string comment;

			Data data;
		};

		SounddiverLibLoader(Data _input);

		const auto& getResults() const { return m_listEntries; }

		static bool isValidData(const Data& _data);

	private:
		void read4CC(char* _dest);
		size_t readUInt32();
		uint8_t readUInt8();
		size_t readVarLen();

		bool m_littleEndian;

		std::vector<uint8_t> m_input;

		baseLib::BinaryStream m_stream;

		std::vector<ListEntry> m_listEntries;
	};
}
