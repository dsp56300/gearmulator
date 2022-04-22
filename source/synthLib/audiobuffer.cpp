#include "audiobuffer.h"

#include <cassert>
#include <cstring>	// memcpy

namespace synthLib
{
	void AudioBuffer::insertZeroes(size_t _size)
	{
		TChannel zeroes;
		zeroes.resize(_size, 0.0f);

		for(size_t c=0; c<m_data.size(); ++c)
		{
			m_data[c].insert(m_data[c].begin(), zeroes.begin(), zeroes.end());
		}
	}

	AudioBuffer::AudioBuffer(size_t _channelCount, const size_t _capacity)
	{
		m_data.resize(_channelCount);
		reserve(_capacity);
	}

	void AudioBuffer::reserve(size_t _capacity)
	{
		for(auto i=0; i<m_data.size(); ++i)
		{
			if(m_data[i].capacity() < _capacity)
				m_data[i].reserve(_capacity);
		}
	}

	void AudioBuffer::resize(size_t _capacity)
	{
		for(auto i=0; i<m_data.size(); ++i)
		{
			m_data[i].resize(_capacity);
		}
	}

	void AudioBuffer::append(const TBuffer& _data)
	{
		assert(_data.size() == m_data.size());

		for(size_t c=0; c<_data.size(); ++c)
		{
			append(m_data[c], &_data[c].front(), _data[c].size());
		}
	}

	void AudioBuffer::append(const float** _data, size_t _size)
	{
		for(size_t c=0; c<m_data.size(); ++c)
		{
			append(m_data[c], _data[c], _size);
		}
	}

	void AudioBuffer::append(const TAudioInputs& _data, size_t _size)
	{
		const auto c = std::min(m_data.size(), _data.size());

		for(size_t i=0; i<c; ++i)
			append(m_data[i], _data[i], _size);
	}

	void AudioBuffer::remove(const size_t _count)
	{
		for(size_t c=0; c<m_data.size(); ++c)
		{
			if(_count >= m_data[c].size())
				m_data[c].clear();
			else
				m_data[c].erase(m_data[c].begin(), m_data[c].begin() + _count);
		}
	}

	void AudioBuffer::fillPointers(TAudioOutputs& _pointers, size_t _offset)
	{
		for(size_t c=0; c<m_data.size(); ++c)
			_pointers[c] = &m_data[c][_offset];
	}

	void AudioBuffer::fillPointers(TAudioInputs& _pointers, size_t _offset) const
	{
		for(size_t c=0; c<m_data.size(); ++c)
			_pointers[c] = &m_data[c][_offset];
	}

	size_t AudioBuffer::size() const
	{
		if(m_data.empty())
			return 0;
		return m_data[0].size();
	}

	void AudioBuffer::append(TChannel& _dst, const float* _data, size_t _size)
	{
		const auto oldSize = _dst.size();
		_dst.resize(oldSize + _size);
		memcpy(&_dst[oldSize], _data, _size * sizeof(float));
	}

}
