#pragma once

#include "JuceHeader.h"

class StdoutLogger : public Logger
{
public:
	StdoutLogger();
	~StdoutLogger() override;
	void logMessage(const String& _message) override;
private:
	JUCE_DECLARE_NON_COPYABLE(StdoutLogger)
	JUCE_DECLARE_NON_MOVEABLE(StdoutLogger)
};
