#pragma once

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <sstream>
#include <vector>

namespace synthLib
{
	class BinaryStream final : std::stringstream
	{
	public:
		using SizeType = uint32_t;

		BinaryStream() = default;

		template<typename T> explicit BinaryStream(const std::vector<T>& _data)
		{
			std::stringstream::write(reinterpret_cast<const char*>(_data.data()), _data.size() * sizeof(T));
			seekg(0);
		}

		// ___________________________________
		// tools
		//

		void toVector(std::vector<uint8_t>& _buffer, bool _append = false)
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
				std::stringstream::read(reinterpret_cast<char*>(&_buffer[currentSize]), size);
			}
			else
			{
				_buffer.resize(size);
				std::stringstream::read(reinterpret_cast<char*>(_buffer.data()), size);
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
			std::stringstream::read(s.data(), size);
			const auto result = _str == s;
			seekg(pos);
			return result;
		}

		uint32_t getWritePos() const
		{
			return (uint32_t)const_cast<BinaryStream&>(*this).tellp();
		}

		uint32_t getReadPos() const
		{
			return (uint32_t)const_cast<BinaryStream&>(*this).tellg();
		}

		void setWritePos(const uint32_t _pos)
		{
			seekp(_pos);
		}

		void setReadPos(const uint32_t _pos)
		{
			seekg(_pos);
		}

		bool endOfStream() const
		{
			return eof() || (getReadPos() == size());
		}

		SizeType size() const
		{
			const auto readPos = getReadPos();
			auto& s = const_cast<BinaryStream&>(*this);
			s.seekg(0, std::ios_base::end);
			const auto size = s.tellg();
			s.seekg(readPos);
			return (SizeType)size;
		}

		// ___________________________________
		// write
		//

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void write(const T& _value)
		{
			std::stringstream::write(reinterpret_cast<const char*>(&_value), sizeof(_value));
		}

		template<typename T, typename = std::enable_if_t<std::is_trivially_copyable_v<T>>> void write(const std::vector<T>& _vector)
		{
			const auto size = static_cast<SizeType>(_vector.size());
			write(size);
			if(size)
				std::stringstream::write(reinterpret_cast<const char*>(_vector.data()), sizeof(T) * size);
		}

		void write(const std::string& _string)
		{
			const auto s = static_cast<SizeType>(_string.size());
			write(s);
			std::stringstream::write(_string.c_str(), s);
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
			std::stringstream::read(reinterpret_cast<char*>(&v), sizeof(v));
			checkFail();
			return v;
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
			std::stringstream::read(reinterpret_cast<char*>(_vector.data()), sizeof(T) * size);
			checkFail();
		}

		std::string readString()
		{
			const auto size = read<SizeType>();
			std::string s;
			s.resize(size);
			std::stringstream::read(s.data(), size);
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

		struct Chunk
		{
			char fourcc[5];
			uint32_t expectedVersion;
			ChunkCallback callback;
		};

		ChunkReader(BinaryStream& _stream) : m_stream(_stream)
		{
		}

		template<size_t N, std::enable_if_t<N == 5, void*> = nullptr>
		void add(char const(&_4Cc)[N], const uint32_t _version, const ChunkCallback& _callback)
		{
			Chunk c;
			strcpy(c.fourcc, _4Cc);
			c.expectedVersion = _version;
			c.callback = _callback;
			supportedChunks.emplace_back(std::move(c));
		}

		void read()
		{
			while(!m_stream.endOfStream())
			{
				char fourCC[5];
				m_stream.read4CC(fourCC);
				const auto version = m_stream.read<uint32_t>();
				const auto length = m_stream.read<SizeType>();

				bool hasReadChunk = false;

				++m_numChunks;

				for (const auto& chunk : supportedChunks)
				{
					if(0 != strcmp(chunk.fourcc, fourCC))
						continue;

					if(version > chunk.expectedVersion)
						break;

					std::vector<uint8_t> chunkData;
					chunkData.reserve(length);
					for(size_t i=0; i<length; ++i)
						chunkData.push_back(m_stream.read<uint8_t>());

					hasReadChunk = true;
					++m_numRead;
					BinaryStream s(chunkData);
					chunk.callback(s, version);
					break;
				}

				if(!hasReadChunk)
					m_stream.setReadPos(m_stream.getReadPos() + length);
			}
		}

		bool tryRead()
		{
			const auto pos = m_stream.getReadPos();
			try
			{
				read();
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
		std::vector<Chunk> supportedChunks;
		uint32_t m_numRead = 0;
		uint32_t m_numChunks = 0;
	};
}
