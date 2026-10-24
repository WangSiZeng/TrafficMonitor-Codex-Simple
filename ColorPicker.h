#pragma once
#include <windows.h>

// Uses the same MFC color dialog as TrafficMonitor (statically linked).
bool ShowGraphColorDialog(HWND parent, COLORREF& color);
