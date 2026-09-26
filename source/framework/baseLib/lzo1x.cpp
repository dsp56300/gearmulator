#include "lzo1x.h"

namespace baseLib
{
	// the control flow of the reference lzo1x_d.ch, with bounds checks
	bool lzo1xDecompress(const std::vector<uint8_t>& _src, std::vector<uint8_t>& _dst)
	{
		size_t ip = 0;
		_dst.clear();

		auto has = [&](const size_t _n) { return ip + _n <= _src.size(); };
		auto literals = [&](const size_t _n)
		{
			if(!has(_n))
				return false;
			_dst.insert(_dst.end(), _src.begin() + ip, _src.begin() + ip + _n);
			ip += _n;
			return true;
		};
		auto match = [&](const size_t _distance, const size_t _n)
		{
			if(_distance > _dst.size())
				return false;
			for(size_t i = 0, p = _dst.size() - _distance; i < _n; ++i)
				_dst.push_back(_dst[p + i]);
			return true;
		};
		// a zero length field continues in the following bytes, 255 per zero byte
		auto length = [&](size_t _t, const size_t _base) -> size_t
		{
			if(_t)
				return _t;
			while(has(1) && _src[ip] == 0)
			{
				_t += 255;
				++ip;
			}
			return has(1) ? _t + _base + _src[ip++] : 0;
		};

		enum class State { Loop, FirstLiteralRun, Match, MatchDone, MatchNext };
		State state = State::Loop;
		size_t t = 0;

		if(!has(1))
			return false;

		if(_src[0] > 17)
		{
			t = _src[ip++] - 17u;
			if(t < 4)
				state = State::MatchNext;
			else if(!literals(t))
				return false;
			else
				state = State::FirstLiteralRun;
		}

		while(true)
		{
			switch(state)
			{
			case State::Loop:
				if(!has(1))
					return false;
				t = _src[ip++];
				if(t >= 16)
				{
					state = State::Match;
					break;
				}
				t = length(t, 15);
				if(!t || !literals(t + 3))
					return false;
				state = State::FirstLiteralRun;
				break;
			case State::FirstLiteralRun:
				if(!has(2))
					return false;
				t = _src[ip++];
				if(t >= 16)
				{
					state = State::Match;
					break;
				}
				if(!match(1 + 0x800 + (t >> 2) + (_src[ip++] << 2), 3))
					return false;
				state = State::MatchDone;
				break;
			case State::Match:
				if(t >= 64)
				{
					if(!has(1) || !match(1 + ((t >> 2) & 7) + (_src[ip++] << 3), (t >> 5) + 1))
						return false;
				}
				else if(t >= 32)
				{
					t = length(t & 31, 31);
					if(!t || !has(2))
						return false;
					const size_t distance = 1 + (_src[ip] >> 2) + (_src[ip + 1] << 6);
					ip += 2;
					if(!match(distance, t + 2))
						return false;
				}
				else if(t >= 16)
				{
					const size_t high = (t & 8) << 11;
					t = length(t & 7, 7);
					if(!t || !has(2))
						return false;
					const size_t distance = high + (_src[ip] >> 2) + (_src[ip + 1] << 6);
					ip += 2;
					if(distance == 0)
						return t == 1 && ip == _src.size();	// end of stream
					if(!match(distance + 0x4000, t + 2))
						return false;
				}
				else
				{
					if(!has(1) || !match(1 + (t >> 2) + (_src[ip++] << 2), 2))
						return false;
				}
				state = State::MatchDone;
				break;
			case State::MatchDone:
				t = _src[ip - 2] & 3u;
				state = t ? State::MatchNext : State::Loop;
				break;
			case State::MatchNext:
				if(!literals(t) || !has(1))
					return false;
				t = _src[ip++];
				state = State::Match;
				break;
			}
		}
	}
}
