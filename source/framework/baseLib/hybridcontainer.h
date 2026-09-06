#pragma once

#include <vector>
#include <array>
#include <stdexcept>
#include <initializer_list>
#include <type_traits>
#include <algorithm>

namespace baseLib
{
	template <typename T, size_t MaxFixedSize>
	class HybridContainer
	{
		static_assert(MaxFixedSize > 0, "MaxFixedSize must be greater than 0");

	public:
		using Iterator = T*;
		using ConstIterator = const T*;

		HybridContainer() = default;

		HybridContainer(HybridContainer&& _source) noexcept
		: m_array(std::move(_source.m_array))
		, m_size(_source.m_size)
		, m_useArray(_source.m_useArray)
		, m_vector(std::move(_source.m_vector))
		{
			_source.clear();
		}

		HybridContainer(const HybridContainer& _source) noexcept
		: m_array(_source.m_array)
		, m_size(_source.m_size)
		, m_useArray(_source.m_useArray)
		, m_vector(_source.m_vector)
		{
		}

		HybridContainer(std::initializer_list<T> _list) : m_size(_list.size()), m_useArray(_list.size() <= MaxFixedSize)
		{
			if(m_useArray)
				std::copy(_list.begin(), _list.end(), m_array.begin());
			else
				m_vector.assign(_list);
		}

		explicit HybridContainer(const std::vector<T>& _list) : m_size(_list.size()), m_useArray(_list.size() <= MaxFixedSize)
		{
			if(m_useArray)
				std::copy(_list.begin(), _list.end(), m_array.begin());
			else
				m_vector = _list;
		}

		HybridContainer(std::vector<T>&& _list) : m_size(_list.size()), m_useArray(false), m_vector(std::move(_list))
		{
		}

		~HybridContainer() = default;

		template<typename U>
		void push_back(U&& _value)
		{
			if (m_useArray)
			{
				if (m_size < MaxFixedSize)
				{
					m_array[m_size++] = std::forward<U>(_value);
					return;
				}

				switchToVector();
			}

			m_vector.push_back(std::forward<U>(_value));
			++m_size;
		}

		template<typename U> void emplace_back(U&& _value)
		{
			push_back<U>(std::forward<U>(_value));
		}

		void pop_back()
		{
			if (m_useArray)
			{
				if (m_size == 0)
					throw std::out_of_range("Container is empty");
				--m_size;
				return;
			}

			m_vector.pop_back();
			--m_size;
		}

		const T& front() const
		{
			if (m_useArray)
			{
				if (m_size == 0)
					throw std::out_of_range("Container is empty");
				return m_array.front();
			}

			return m_vector.front();
		}

		const T& back()	const
		{
			if (m_useArray)
			{
				if (m_size == 0)
					throw std::out_of_range("Container is empty");
				return m_array[m_size - 1];
			}

			return m_vector.back();
		}

		bool empty() const
		{
			return m_size == 0;
		}

		size_t size() const
		{
			return m_size;
		}

		void clear()
		{
			m_size = 0;
			m_useArray = true;
		}

		ConstIterator begin() const
		{
			if (m_useArray)
				return m_array.data();
			return m_vector.data();
		}

		Iterator begin()
		{
			if (m_useArray)
				return m_array.data();
			return m_vector.data();
		}

		ConstIterator end() const { return begin() + m_size; }
		Iterator end() { return begin() + m_size; }

		void swap(HybridContainer& _other) noexcept
		{
			std::swap(m_array, _other.m_array);
			std::swap(m_size, _other.m_size);
			std::swap(m_useArray, _other.m_useArray);
			std::swap(m_vector, _other.m_vector);
		}

		void swap(std::vector<T>& _other) noexcept
		{
			_other.swap(m_vector);

			if (m_useArray && m_size)
				_other.assign(m_array.begin(), m_array.begin() + m_size);

			m_useArray = false;
			m_size = m_vector.size();
		}

		template<typename U>
		void append(const U& _data)
		{
			if (_data.size() + size() > MaxFixedSize)
			{
				switchToVector();
				m_vector.insert(m_vector.end(), _data.begin(), _data.end());
			}
			else if (!m_useArray)
			{
				m_vector.insert(m_vector.end(), _data.begin(), _data.end());
			}
			else
			{
				std::copy(_data.begin(), _data.end(), m_array.begin() + m_size);
			}

			m_size += _data.size();
		}

		void assign(const T* _data, const size_t _size)
		{
			if (_size > MaxFixedSize)
			{
				switchToVector();
				m_vector.assign(_data, _data + _size);
			}
			else if (!m_useArray)
			{
				m_vector.assign(_data, _data + _size);
			}
			else
			{
				std::copy(_data, _data + _size, m_array.begin());
			}
			m_size = _size;
		}

		template<size_t Size> void assign(const std::array<T, Size>& _data) { assign(_data.begin(), _data.end()); }
		void assign(const std::vector<T>& _data)                            { assign(_data.begin(), _data.end()); }
		void assign(const std::initializer_list<T>& _data)                  { assign(_data.begin(), _data.end()); }
		void assign(ConstIterator _first, ConstIterator _last)              { assign(_first, _last - _first); }

		void erase(const ConstIterator _first, const ConstIterator _last)
		{
			if (_first < begin() || _last > end())
				throw std::out_of_range("Iterator out of range");

			const auto offset = _first - begin();
			const auto count = _last - _first;

			if (m_useArray)
			{
				std::copy(_last, end(), begin() + offset);
				m_size -= count;
				return;
			}

			m_vector.erase(m_vector.begin() + offset, m_vector.begin() + offset + count);
			m_size = m_vector.size();
		}

		void erase(const ConstIterator _position) { erase(_position, _position + 1); }

		void insert(const ConstIterator _position, const T& _value)
		{
			if (_position < begin() || _position > end())
				throw std::out_of_range("Iterator out of range");

			const auto offset = _position - begin();

			if (m_useArray)
			{
				if (m_size == MaxFixedSize)
				{
					switchToVector();
				}
				else
				{
					std::copy_backward(begin() + offset, end(), end() + 1);
					m_array[offset] = _value;
					++m_size;
					return;
				}
			}

			m_vector.insert(m_vector.begin() + offset, _value);
			++m_size;
		}

		template<typename Iter>
		void insert(const ConstIterator _position, Iter _first, Iter _last)
		{
			if (_position < begin() || _position > end())
				throw std::out_of_range("Iterator out of range");

			const auto offset = _position - begin();

			if (m_useArray)
			{
				if (m_size + (_last - _first) > MaxFixedSize)
				{
					switchToVector();
				}
				else
				{
					const auto count = _last - _first;
					std::copy_backward(begin() + offset, end(), end() + count);
					std::copy(_first, _last, begin() + offset);
					m_size += count;
					return;
				}
			}

			m_vector.insert(m_vector.begin() + offset, _first, _last);
			m_size = m_vector.size();
		}
		
		void reserve(size_t _size, const bool _switchToVectorIfNeeded = false)
		{
			if (_size < MaxFixedSize)
				return;

			if (m_useArray && !_switchToVectorIfNeeded)
				return;

			switchToVector();
			m_vector.reserve(_size);
		}

		const T* data() const
		{
			if (m_useArray)
				return m_array.data();
			return m_vector.data();
		}

		T* data()
		{
			if (m_useArray)
				return m_array.data();
			return m_vector.data();
		}

		T& operator[](const size_t _index)
		{
			if (m_useArray)
			{
				if (_index >= m_size)
					throw std::out_of_range("Index out of range");

				return m_array[_index];
			}

			return m_vector[_index];
		}

		const T& operator[](const size_t _index) const
		{
			return const_cast<HybridContainer*>(this)->operator[](_index);
		}

		HybridContainer& operator=(const HybridContainer& _source)
		{
			if (this == &_source)
				return *this;

			m_size = _source.m_size;
			m_useArray = _source.m_useArray;

			if (m_useArray)
				m_array = _source.m_array;
			else
				m_vector = _source.m_vector;

			return *this;
		}

		HybridContainer& operator=(HybridContainer&& _source) noexcept
		{
			if (this == &_source)
				return *this;

			m_size = _source.m_size;
			m_useArray = _source.m_useArray;

			if (m_useArray)
				m_array = std::move(_source.m_array);
			else
				m_vector = std::move(_source.m_vector);

			return *this;
		}

		template<size_t OtherFixedSize> HybridContainer& operator=(const HybridContainer<T, OtherFixedSize>& _source)
		{
			m_size = _source.size();

			if(_source.empty())
			{
				m_useArray = true;
				return *this;
			}

			if(m_size <= MaxFixedSize)
			{
				std::copy(_source.begin(), _source.end(), m_array.begin());
				m_useArray = true;
			}
			else
			{
				m_vector.assign(_source.begin(), _source.end());
				m_useArray = false;
			}

			return *this;
		}

		template<size_t OtherFixedSize> HybridContainer& operator=(HybridContainer<T, OtherFixedSize>&& _source)
		{
			m_size = _source.size();

			if(!m_size)
			{
				m_useArray = true;
				_source.clear();
				return *this;
			}

			if(m_size <= MaxFixedSize)
			{
				std::move(_source.begin(), _source.end(), m_array.begin());
				m_useArray = true;
			}
			else if(!_source.m_useArray)
			{
				m_vector = std::move(_source.m_vector);
				m_useArray = false;
			}
			else
			{
				m_vector.assign(_source.begin(), _source.end());
				m_useArray = false;
			}

			_source.clear();

			return *this;
		}

		HybridContainer& operator=(std::vector<T>&& _source)
		{
			m_vector = std::move(_source);
			m_size = m_vector.size();
			m_useArray = false;

			return *this;
		}

		HybridContainer& operator=(const std::vector<T>& _source)
		{
			if(_source.size() <= MaxFixedSize)
			{
				m_size = _source.size();
				m_useArray = true;
				m_vector.clear();
				std::copy(_source.begin(), _source.end(), m_array.begin());
			}
			else
			{
				m_vector = _source;
				m_size = m_vector.size();
				m_useArray = false;
			}
			return *this;
		}

		void resize(size_t _size)
		{
			if (_size <= MaxFixedSize)
			{
				for (size_t i=m_size; i<_size; ++i)
					m_array[i] = T();
				m_size = _size;
				return;
			}
			m_vector.resize(_size);
			m_size = _size;
		}

	private:
		void switchToVector()
		{
			if (!m_useArray)
				return;
			m_useArray = false;
			m_vector.assign(m_array.begin(), m_array.begin() + m_size);
		}

		std::array<T, MaxFixedSize> m_array;
		size_t m_size = 0;
		bool m_useArray = true;
		std::vector<T> m_vector;
	};
}
