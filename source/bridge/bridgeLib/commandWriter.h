#pragma once

#include "commands.h"

#include "baseLib/binarystream.h"

namespace networkLib
{
	class Stream;
}

namespace bridgeLib
{
	class CommandWriter
	{
	public:
		CommandWriter();

		baseLib::BinaryStream& build(Command _command);

		baseLib::BinaryStream& build(const Command _command, const CommandStruct& _data);

		void write(networkLib::Stream& _stream, bool _flush = true);
		void write(baseLib::BinaryStream& _out);

	private:
		baseLib::BinaryStream m_stream;
		Command m_command = Command::Invalid;
	};
}
