#pragma once

#include <cstdint>

#include "stream.h"

namespace ptypes
{
	class ipstream;
}

namespace networkLib
{
	class TcpStream : public Stream
	{
	public:
		explicit TcpStream(ptypes::ipstream* _stream);
		virtual ~TcpStream();

		void close() override;
		bool isValid() const override;
		bool flush() override;

		auto* getPtypesStream() const { return m_stream; }

	private:
		bool read(void* _buf, uint32_t _byteSize) override;
		bool write(const void* _buf, uint32_t _byteSize) override;

		ptypes::ipstream* m_stream;
	};
}
