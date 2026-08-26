#pragma once

namespace baseLib
{
	class BinaryStream;
}

namespace bridgeLib
{
	class CommandStruct
	{
	public:
		virtual ~CommandStruct() = default;

		virtual baseLib::BinaryStream& write(baseLib::BinaryStream& _s) const = 0;
		virtual baseLib::BinaryStream& read(baseLib::BinaryStream& _s) = 0;
	};
}
