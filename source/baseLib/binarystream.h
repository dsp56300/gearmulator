#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <sstream>
#include <vector>
#include <cstring>

namespace baseLib
{
	class StreamBuffer
	{
	public:
		StreamBuffer() = default;
		explicit StreamBuffer(std::vector<uint8_t>&& _buffer) : m_vector(std::move(_buffer))
		{
		}
		explicit StreamBuffer(const size_t _capacity)
		{
			m_vector.reserve(_capacity);
		}
		StreamBuffer(uint8_t* _buffer, const size_t _size) : m_buffer(_buffer), m_size(_size), m_fixedSize(true)
		{
		}
		StreamBuffer(StreamBuffer& _parent, const size_t _childSize) : m_buffer(&_parent.buffer()[_parent.tellg()]), m_size(_childSize), m_fixedSize(true)
		{
			// force eof if range is not valid
			if(_parent.tellg() + _childSize > _parent.size())
			{
				assert(false && "invalid range");
				m_readPos = _childSize;
			}

			// seek parent forward
			_parent.seekg(_parent.tellg() + _childSize);
		}
		StreamBuffer(StreamBuffer&& _source) noexcept
			: m_buffer(_source.m_buffer)
			, m_size(_source.m_size)
			, m_fixedSize(_source.m_fixedSize)
			, m_readPos(_source.m_readPos)
			, m_writePos(_source.m_writePos)
			, m_vector(std::move(_source.m_vector))
			, m_fail(_source.m_fail)
		{
			_source.destroy();
		}

		StreamBuffer& operator = (StreamBuffer&& _source) noexcept
		{
			m_buffer = _source.m_buffer;
			m_size = _source.m_size;
			m_fixedSize = _source.m_fixedSize;
			m_readPos = _source.m_readPos;
			m_writePos = _source.m_writePos;
			m_vector = std::move(_source.m_vector);

			_source.destroy();

			return *this;
		}

		void seekg(const size_t _pos)		{ m_readPos = _pos; }
		size_t tellg() const				{ return m_readPos; }
		void seekp(const size_t _pos)		{ m_writePos = _pos; }
		size_t tellp() const				{ return m_writePos; }
		bool eof() const					{ return tellg() >= size(); }
		bool fail() const					{ return m_fail; }

		bool read(uint8_t* _dst, size_t _size)
		{
			const auto remaining = size() - tellg();
			if(remaining < _size)
			{
				m_fail = true;
				return false;
			}
			::memcpy(_dst, &buffer()[m_readPos], _size);
			m_readPos += _size;
			return true;
		}
		bool write(const uint8_t* _src, size_t _size)
		{
			const auto remaining = size() - tellp();
			if(remaining < _size)
			{
				if(m_fixedSize)
				{
					m_fail = true;
					return false;
				}
				m_vector.resize(tellp() + _size);
			}
			::memcpy(&buffer()[m_writePos], _src, _size);
			m_writePos += _size;
			return true;
		}

		explicit operator bool () const
		{
			return !eof();
		}

	private:
		size_t size() const					{ return m_fixedSize ? m_size : m_vector.size(); }

		uint8_t* buffer()
		{
			if(m_fixedSize)
				return m_buffer;
			return m_vector.data();
		}

		void destroy()
		{
			m_buffer = nullptr;
			m_size = 0;
			m_fixedSize = false;
			m_readPos = 0;
			m_writePos = 0;
			m_vector.clear();
			m_fail = false;
		}

		uint8_t* m_buffer = nullptr;
		size_t m_size = 0;
		bool m_fixedSize = false;
		size_t m_readPos = 0;
		size_t m_writePos = 0;
		std::vector<uint8_t> m_vector;
		bool m_fail = false;
	};

	using StreamSizeType = uint32_t;

	class BinaryStream final : StreamBuffer
	{
	public:
		using Base = StreamBuffer;
		using SizeType = StreamSizeType;

		BinaryStream() = default;

		using StreamBuffer::operator bool;

		explicit BinaryStream(BinaryStream& _parent, SizeType _length) : StreamBuffer(_parent, _length)
		{
		}

		template<typename T> explicit BinaryStream(const std::vector<T>& _data)
		{
			Base::write(reinterpret_cast<const uint8_t*>(_data.data()), _data.size() * sizeof(T));
			seekg(0);
		}

		// ___________________________________
		// tools
		//

		void toVector(std::vector<uint8_t>& _buffer, const bool _append = false)
		{
			const auto size = tellp();
			if(size <= 0)
			{
				if(!_append)
					_buffer.clear();
				return;
			}

			seekg(0);

			if(_append)
			{
				const auto currentSize = _buffer.size();
				_buffer.resize(currentSize + size);
				Base::read(&_buffer[currentSize], size);
			}
			else
			{
				_buffer.resize(size);
				Base::read(_buffer.data(), size);
			}
		}

		bool checkString(const std::string& _str)
		{
			const auto pos = tellg();
			
			const auto size = read<SizeType>();
			if (size != _str.size())
			{
				seekg(pos);
				return false;
			}
			std::string s;
			s.resize(size);
			Base::read(reinterpret_cast<uint8_t*>(s.data()), size);
			const auto result = _str == s;
			seekg(pos);
			return result;
		}

		uint32_t getWritePos() const			{ return static_cast<uint32_t>(tellp()); }
		uint32_t getReadPos() const				{ return static_cast<uint32_t>(tellg()); }
		bool endOfStream() const				{ return eof(); }

		void setWritePos(const uint32_t _pos)	{ seekp(_pos); }
		void setReadPos(const uint32_t _pos)	{ seekg(_pos); }

		// ___________________________________
		// write
		//

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void write(const T& _value)
		{
			Base::write(reinterpret_cast<const uint8_t*>(&_value), sizeof(_value));
		}

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void write(const T* _data, const size_t _size)
		{
			if(!_size)
				return;
			Base::write(reinterpret_cast<const uint8_t*>(_data), sizeof(T) * _size);
		}

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void write(const std::vector<T>& _vector)
		{
			const auto size = static_cast<SizeType>(_vector.size());
			write(size);
			if(size)
				Base::write(reinterpret_cast<const uint8_t*>(_vector.data()), sizeof(T) * size);
		}

		void write(const std::string& _string)
		{
			const auto s = static_cast<SizeType>(_string.size());
			write(s);
			Base::write(reinterpret_cast<const uint8_t*>(_string.c_str()), s);
		}

		void write(const char* const _value)
		{
			write(std::string(_value));
		}

		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		void write4CC(char const(&_str)[N])
		{
			write(_str[0]);
			write(_str[1]);
			write(_str[2]);
			write(_str[3]);
		}


		// ___________________________________
		// read
		//

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> T read()
		{
			T v{};
			Base::read(reinterpret_cast<uint8_t*>(&v), sizeof(v));
			checkFail();
			return v;
		}

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> T& read(T& _dst)
		{
			Base::read(reinterpret_cast<uint8_t*>(&_dst), sizeof(_dst));
			checkFail();
			return _dst;
		}

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void read(T* _out, const size_t _size)
		{
			if(_size)
				Base::read(reinterpret_cast<uint8_t*>(_out), sizeof(T) * _size);
		}

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void read(std::vector<T>& _vector)
		{
			const auto size = read<SizeType>();
			checkFail();
			if (!size)
			{
				_vector.clear();
				return;
			}
			_vector.resize(size);
			Base::read(reinterpret_cast<uint8_t*>(_vector.data()), sizeof(T) * size);
			checkFail();
		}

		std::string readString()
		{
			const auto size = read<SizeType>();
			std::string s;
			s.resize(size);
			Base::read(reinterpret_cast<uint8_t*>(s.data()), size);
			checkFail();
			return s;
		}

		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		void read4CC(char const(&_str)[N])
		{
			char res[5];
			read4CC(res);

			return strcmp(res, _str) == 0;
		}

		void read4CC(std::array<char, 5>& _fourCC)
		{
			_fourCC.fill(0);

			_fourCC[0] = read<char>();
			_fourCC[1] = read<char>();
			_fourCC[2] = read<char>();
			_fourCC[3] = read<char>();
		}

		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		void read4CC(char (&_str)[N])
		{
			_str[0] = 'E';
			_str[1] = 'R';
			_str[2] = 'R';
			_str[3] = 'R';
			_str[4] = 0;

			_str[0] = read<char>();
			_str[1] = read<char>();
			_str[2] = read<char>();
			_str[3] = read<char>();
		}

		BinaryStream readChunk();
		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		BinaryStream tryReadChunk(char const(&_4Cc)[N], const uint32_t _version = 1)
		{
			return tryReadChunkInternal(_4Cc, _version);
		}

	private:
		BinaryStream tryReadChunkInternal(const char* _4Cc, uint32_t _version = 1);


		// ___________________________________
		// helpers
		//

	private:
		void checkFail() const
		{
			if(fail())
				throw std::range_error("end-of-stream");
		}
	};

	struct Chunk
	{
		using SizeType = BinaryStream::SizeType;

		char fourCC[5];
		uint32_t version;
		SizeType length;
		BinaryStream data;

		bool read(BinaryStream& _parentStream)
		{
			_parentStream.read4CC(fourCC);
			version = _parentStream.read<uint32_t>();
			length = _parentStream.read<SizeType>();
			data = BinaryStream(_parentStream, length);
			return !data.endOfStream();
		}
	};

	class ChunkWriter
	{
	public:
		using SizeType = BinaryStream::SizeType;

		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		ChunkWriter(BinaryStream& _stream, char const(&_4Cc)[N], const uint32_t _version = 1) : m_stream(_stream)
		{
			m_stream.write4CC(_4Cc);
			m_stream.write(_version);
			m_lengthWritePos = m_stream.getWritePos();
			m_stream.write<SizeType>(0);
		}

		ChunkWriter() = delete;
		ChunkWriter(ChunkWriter&&) = delete;
		ChunkWriter(const ChunkWriter&) = delete;
		ChunkWriter& operator = (ChunkWriter&&) = delete;
		ChunkWriter& operator = (const ChunkWriter&) = delete;

		~ChunkWriter()
		{
			const auto currentWritePos = m_stream.getWritePos();
			const SizeType chunkDataLength = currentWritePos - m_lengthWritePos - sizeof(SizeType);
			m_stream.setWritePos(m_lengthWritePos);
			m_stream.write(chunkDataLength);
			m_stream.setWritePos(currentWritePos);
		}

	private:
		BinaryStream& m_stream;
		SizeType m_lengthWritePos = 0;
	};

	class ChunkReader
	{
	public:
		using SizeType = ChunkWriter::SizeType;
		using ChunkCallback = std::function<void(BinaryStream&, uint32_t)>;	// data, version

		struct ChunkCallbackData
		{
			char fourCC[5];
			uint32_t expectedVersion;
			ChunkCallback callback;
		};

		explicit ChunkReader(BinaryStream& _stream) : m_stream(_stream)
		{
		}

		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		void add(char const(&_4Cc)[N], const uint32_t _version, const ChunkCallback& _callback)
		{
			ChunkCallbackData c;
			strcpy(c.fourCC, _4Cc);
			c.expectedVersion = _version;
			c.callback = _callback;
			supportedChunks.emplace_back(std::move(c));
		}

		void read(const uint32_t _count = 0)
		{
			uint32_t count = 0;

			while(!m_stream.endOfStream() && (!_count || ++count <= _count))
			{
				Chunk chunk;
				chunk.read(m_stream);

				++m_numChunks;

				for (const auto& chunkData : supportedChunks)
				{
					if(0 != strcmp(chunkData.fourCC, chunk.fourCC))
						continue;

					if(chunk.version > chunkData.expectedVersion)
						break;

					++m_numRead;
					chunkData.callback(chunk.data, chunk.version);
					break;
				}
			}
		}

		bool tryRead(const uint32_t _count = 0)
		{
			const auto pos = m_stream.getReadPos();
			try
			{
				read(_count);
				return true;
			}
			catch(std::range_error&)
			{
				m_stream.setReadPos(pos);
				return false;
			}
		}

		uint32_t numRead() const
		{
			return m_numRead;
		}

		uint32_t numChunks() const
		{
			return m_numChunks;
		}

	private:
		BinaryStream& m_stream;
		std::vector<ChunkCallbackData> supportedChunks;
		uint32_t m_numRead = 0;
		uint32_t m_numChunks = 0;
	};
}
