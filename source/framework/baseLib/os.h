#pragma once

namespace baseLib
{
	void setFlushDenormalsToZero();
	bool isRunningUnderRosetta();

	// For console tools that have to fail instead of waiting for a click: a failed assert or an abort()
	// reports to stderr and terminates the process instead of opening a modal dialog. No-op off MSVC.
	void disableErrorDialogs();
}
