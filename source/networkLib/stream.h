#pragma once

namespace networkLib
{
	class Stream
	{
	public:
		virtual void close() = 0;
		virtual bool isValid() const = 0;
		virtual bool flush() = 0;

		virtual bool read(void* _buf, uint32_t _byteSize) = 0;
		virtual bool write(const void* _buf, uint32_t _byteSize) = 0;
	};
}
