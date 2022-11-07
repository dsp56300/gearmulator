#pragma once

#include "audioOutput.h"

#include "../synthLib/wavWriter.h"

class AudioOutputWAV : AudioOutput
{
public:
	explicit AudioOutputWAV(const ProcessCallback& _callback);
	~AudioOutputWAV() override;

	void threadFunc();

private:
	synthLib::AsyncWriter wavWriter;

	std::vector<dsp56k::TWord> m_stereoOutput;
	std::unique_ptr<std::thread> m_thread;
	bool silence = true;
	bool m_terminate = false;
};
