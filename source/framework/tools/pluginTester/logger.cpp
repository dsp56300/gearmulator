#include "logger.h"

StdoutLogger::StdoutLogger()
{
	setCurrentLogger(this);
}

StdoutLogger::~StdoutLogger()
{
	setCurrentLogger(nullptr);
}

void ::StdoutLogger::logMessage(const String& _message)
{
	puts(_message.toStdString().c_str());
}
