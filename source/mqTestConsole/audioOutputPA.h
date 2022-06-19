#pragma once
#include "audioOutput.h"

class AudioOutputPA : public AudioOutput
{
public:
	AudioOutputPA(ProcessCallback _callback);

	void portAudioCallback(void* _dst, uint32_t _frames) const;

	void process() override;
private:
	void* m_stream = nullptr;
};
