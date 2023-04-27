#pragma once

#include <cstdint>
#include <iosfwd>
#include <sstream>
#include <vector>

namespace synthLib
{
	template<typename SizeType>
	class BinaryStream final : std::stringstream
	{
	public:
		BinaryStream() = default;

		template<typename T> explicit BinaryStream(const std::vector<T>& _data)
		{
			std::stringstream::write(reinterpret_cast<const char*>(_data.data()), _data.size() * sizeof(T));
			seekg(0);
		}

		// ___________________________________
		// tools
		//

		void toVector(std::vector<uint8_t>& _buffer)
		{
			const auto size = tellp();
			if(size <= 0)
			{
				_buffer.clear();
				return;
			}
			_buffer.resize(size);
			seekg(0);
			std::stringstream::read(reinterpret_cast<char*>(_buffer.data()), size);
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
}
