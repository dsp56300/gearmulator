#include "rmlNativeWindow.h"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#endif

namespace juceRmlUi
{
#ifdef _WIN32
	namespace
	{
		LRESULT CALLBACK eraseBlack(HWND _hwnd, UINT _msg, WPARAM _wParam, LPARAM _lParam, UINT_PTR _id, DWORD_PTR)
		{
			switch (_msg)
			{
			case WM_ERASEBKGND:
				{
					RECT r;
					GetClientRect(_hwnd, &r);
					FillRect(reinterpret_cast<HDC>(_wParam), &r, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
					return 1;
				}
			case WM_NCDESTROY:
				RemoveWindowSubclass(_hwnd, eraseBlack, _id);
				break;
			default:
				break;
			}
			return DefSubclassProc(_hwnd, _msg, _wParam, _lParam);
		}
	}
#endif

	void setBlackWindowBackground(void* _nativeWindowHandle)
	{
#ifdef _WIN32
		// the same subclass again only replaces itself
		if (_nativeWindowHandle)
			SetWindowSubclass(static_cast<HWND>(_nativeWindowHandle), eraseBlack, 1, 0);
#else
		(void)_nativeWindowHandle;
#endif
	}
}
