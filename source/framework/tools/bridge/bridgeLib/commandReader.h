#pragma once

#include "commands.h"

#include "baseLib/binarystream.h"

namespace networkLib
{
	class Stream;
}

namespace bridgeLib
{
	class CommandReader
	{
	public:
		using CommandCallback = std::function<void(Command, baseLib::BinaryStream&)>;

		CommandReader(CommandCallback&& _callback);
		virtual ~CommandReader() = default;

		void read(networkLib::Stream& _stream);
		void read(baseLib::BinaryStream& _in);

		virtual void handleCommand(Command _command, baseLib::BinaryStream& _in)
		{
			m_commandCallback(_command, _in);
		}

	private:
		baseLib::BinaryStream m_stream;
		CommandCallback m_commandCallback;
	};
}
