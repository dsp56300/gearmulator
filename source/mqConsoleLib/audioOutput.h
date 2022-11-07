#pragma once

#include <functional>

#include "../mqLib/mqtypes.h"

class AudioOutput
{
public:
	using ProcessCallback = std::function<void(uint32_t, const mqLib::TAudioOutputs*&)>;

	AudioOutput(const ProcessCallback& _callback) : m_processCallback(_callback)
	{
	}
	virtual ~AudioOutput() = default;

	virtual void process() {}

protected:
	const ProcessCallback& m_processCallback;
};
