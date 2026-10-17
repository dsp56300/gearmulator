#include "config.h"

namespace pluginManager
{
	Urls& urls()
	{
		static Urls u;
		return u;
	}
}
