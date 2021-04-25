#include "plugin.h"

#include "../dsp56300/source/dsp56kEmu/unittests.h"

namespace virusLib
{
	Plugin::Plugin()
	{
		dsp56k::UnitTests tests;
	}
}
