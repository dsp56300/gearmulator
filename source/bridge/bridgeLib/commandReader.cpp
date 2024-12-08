#include "commandReader.h"

#include "command.h"
#include "dsp56300/source/dsp56kEmu/logging.h"
#include "networkLib/stream.h"

namespace bridgeLib
{
	CommandReader::CommandReader(CommandCallback&& _callback) : m_stream(512 * 1024), m_commandCallback(std::move(_callback))
	{
	}

	void CommandReader::read(networkLib::Stream& _stream)
	{
		// read command (4 bytes)
		char temp[5]{0,0,0,0,0};

		_stream.read(temp, 4);
		const auto command = static_cast<Command>(cmd(temp));

		// read size (4 bytes)
		uint32_t size;
		_stream.read(&size, sizeof(size));

		// read data (n bytes)
		m_stream.getVector().resize(size);
		_stream.read(m_stream.getVector().data(), size);

//		LOG("Recv cmd " << commandToString(command) << ", len " << size);
		m_stream.setReadPos(0);
		handleCommand(command, m_stream);
	}

	void CommandReader::read(baseLib::BinaryStream& _in)
	{
		// read command (4 bytes)
		char temp[5]{0,0,0,0,0};

		_in.read(temp[0]);
		_in.read(temp[1]);
		_in.read(temp[2]);
		_in.read(temp[3]);

		const auto command = cmd(temp);

		// read size (4 bytes)
		const uint32_t size = _in.read<uint32_t>();

		// read data (n bytes)
		m_stream.getVector().resize(size);
		for(size_t i=0; i<size; ++i)
			m_stream.getVector()[i] = _in.read<uint8_t>();

		m_stream.setReadPos(0);
		handleCommand(static_cast<Command>(command), m_stream);
	}
}
