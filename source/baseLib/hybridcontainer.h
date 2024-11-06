#pragma once

#include <vector>
#include <array>

namespace baseLib
{
	template <typename T, size_t MaxFixedSize>
	class HybridContainer
	{
		static_assert(MaxFixedSize > 0, "MaxFixedSize must be greater than 0");

	public:
		HybridContainer() {}

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

		T& operator[](const size_t _index)
		{
			if (m_useArray)
			{
				if (_index >= m_size)
				{
					throw std::out_of_range("Index out of range");
				}
				return m_array[_index];
			}

			return m_vector[_index];
		}

		const T& operator[](const size_t _index) const
		{
			return const_cast<HybridContainer*>(this)->operator[](_index);
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

		auto begin() const
		{
			if (m_useArray)
				return &m_array[0];

			return &m_vector[0];
		}

		auto end() const
		{
			return begin() + m_size;
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

	private:
		void switchToVector()
		{
			m_useArray = false;
			m_vector.assign(m_array.begin(), m_array.begin() + m_size);
		}

		std::array<T, MaxFixedSize> m_array;
		size_t m_size = 0;
		bool m_useArray = true;
		std::vector<T> m_vector;
	};
}
