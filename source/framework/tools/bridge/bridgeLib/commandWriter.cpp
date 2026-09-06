#include "commandWriter.h"

#include <array>

#include "networkLib/stream.h"

namespace bridgeLib
{
	CommandWriter::CommandWriter() : m_stream(512 * 1024)
	{
	}

	baseLib::BinaryStream& CommandWriter::build(const Command _command)
	{
		m_stream.setWritePos(0);
		m_command = _command;

		return m_stream;
	}

	baseLib::BinaryStream& CommandWriter::build(const Command _command, const CommandStruct& _data)
	{
		return _data.write(build(_command));
	}

	void CommandWriter::write(networkLib::Stream& _stream, const bool _flush)
	{
		// send command (4 bytes)
		std::array<char,5> buf;
		commandToBuffer(buf, m_command);
		_stream.write(buf.data(), 4);

		// send size (4 bytes)
		const auto size = m_stream.getWritePos();
		_stream.write(&size, sizeof(size));

		// send data (size bytes)
		const auto* data = m_stream.getVector().data();
		_stream.write(data, size);

		if(_flush)
			_stream.flush();
	}

	void CommandWriter::write(baseLib::BinaryStream& _out)
	{
		// send command (4 bytes)
		std::array<char,5> buf;
		commandToBuffer(buf, m_command);
		_out.write(buf[0]);
		_out.write(buf[1]);
		_out.write(buf[2]);
		_out.write(buf[3]);

		// send size (4 bytes)
		const auto size = m_stream.getWritePos();
		_out.write(size);

		// send data (size bytes)
		const auto* data = m_stream.getVector().data();
		for(uint32_t i=0; i<size; ++i)
			_out.write(m_stream.getVector()[i]);
	}
}
