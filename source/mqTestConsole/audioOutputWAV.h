#pragma once

#include "audioOutput.h"

#include "../synthLib/wavWriter.h"

class AudioOutputWAV : AudioOutput
{
public:
	explicit AudioOutputWAV(ProcessCallback _callback);

	void process() override;

private:
	synthLib::AsyncWriter wavWriter;

	std::vector<dsp56k::TWord> m_stereoOutput;
	bool silence = true;
};
