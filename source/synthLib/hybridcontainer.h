#pragma once

#include <vector>
#include <array>

namespace syntLib
{
	template<typename T, size_t S> class HybridVector
	{
	public:
		HybridVector(const HybridVector& _source) : m_array(_source.m_array), m_size(_source.m_size), m_vector(_source.m_vector ? new std::vector<T>(*_source.m_vector) : nullptr)
		{
		}
		HybridVector(HybridVector&& _source) noexcept : m_array(std::move(_source.m_array)), m_size(_source.m_size), m_vector(_source.m_vector)
		{
			_source.m_size = 0;
			_source.m_vector = nullptr;
		}
		~HybridVector()
		{
			delete m_vector;
		}
		HybridVector& operator = (HybridVector&& _source) noexcept
		{
			m_array = _source.m_array;
			m_size = _source.m_size;
			m_vector = _source.m_vector;

			_source.m_size = 0;
			_source.m_vector = nullptr;

			return *this;
		}
		HybridVector& operator = (const HybridVector& _source) noexcept  // NOLINT(bugprone-unhandled-self-assignment) WTF, I handle it?!
		{
			if(&_source == this)
				return *this;

			m_array = _source.m_array;
			m_size = _source.m_size;

			if(_source.m_vector)
				m_vector = new std::vector<T>(*_source.m_vector);

			return *this;
		}

		HybridVector& operator = (const std::vector<T>& _source) noexcept
		{
			m_size = _source.size();

			if(_source.size() <= S)
			{
				std::copy_n(_source.begin(), _source.size(), m_array.begin());
			}
			else
			{
				if(!m_vector)
				{
					m_vector = new std::vector<T>(_source);
				}
				else
				{
					m_vector->clear();
					m_vector->reserve(_source.size());
					m_vector->insert(m_vector->begin(), _source.begin(), _source.end());
				}
			}
			return *this;
		}

		HybridVector& operator = (std::vector<T>&& _source) noexcept
		{
			m_size = _source.size();

			if(_source.size() <= S)
			{
				std::copy_n(_source.begin(), _source.size(), m_array.begin());
			}
			else
			{
				if(!m_vector)
					m_vector = new std::vector<T>(_source);

				std::swap(*m_vector, _source);
			}
			return *this;
		}

		size_t size() const
		{
			return m_size;
		}

		bool empty() const
		{
			return m_size == 0;
		}

		void clear()
		{
			delete m_vector;
			m_vector = nullptr;
			m_size = 0;
		}

		bool isDynamic() const
		{
			return m_vector != nullptr;
		}

		void push_back(const T& _value)
		{
			makeDynamicIfNeeded();

			if(m_vector)
				m_vector->push_back(_value);
			else
				m_array[m_size] = _value;
			++m_size;
		}

		void push_back(const T* _data, size_t _size)
		{
			if(!_size)
				return;

			makeDynamicIfNeeded(_size);

			if(m_vector)
				m_vector->insert(m_vector->end(), _data, _data + _size);
			else
				std::copy_n(_data, _size, &m_array[m_size]);
			m_size += _size;
		}

		void push_back(const std::vector<T>& _data)
		{
			if(_data.empty())
				return;
			push_back(&_data[0], _data.size());
		}

		template<typename T2, size_t S2> void push_back(const HybridVector<T2,S2>& _source)
		{
			push_back(_source.begin(), _source.size());
		}

		T* begin()
		{
			if(m_vector)
				return &m_vector->front();
			return &m_array[0];
		}

		T* end()
		{
			return begin() + size();
		}

		void pop_back()
		{
			if(empty())
				return;

			if(m_vector)
				m_vector->pop_back();
			--m_size;
		}

		const T& front() const
		{
			if(m_vector)
				return m_vector->front();
			return m_array.front();
		}

		const T& back() const
		{
			if(m_vector)
				return m_vector->back();
			return m_array.back();
		}

		void swap(HybridVector& _source) noexcept
		{
			if(!m_vector || !_source.m_vector)
				std::swap(m_array, _source.m_array);

			std::swap(m_size, _source.m_size);
			std::swap(m_vector, _source.m_vector);
		}

	private:
		bool makeDynamicIfNeeded(const size_t _elementCountToAdd = 1)
		{
			if(size() + _elementCountToAdd <= S)
				return false;

			makeDynamic();
			return true;
		}

		void makeDynamic()
		{
			if(!m_vector)
				m_vector = new std::vector<T>();
			m_vector->reserve(m_size);
			std::copy_n(m_array.begin(), m_size, m_vector->begin());
		}

		std::array<T, S> m_array;
		size_t m_size = 0;
		std::vector<T>* m_vector = nullptr;
	};
}
