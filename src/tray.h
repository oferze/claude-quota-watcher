#pragma once
#include <windows.h>

#include "state.h"

constexpr UINT WM_APP_TRAY = WM_APP + 2;  // tray callback; LOWORD(lParam) = event
constexpr UINT IDM_SETTINGS = 1001;
constexpr UINT IDM_EXIT = 1002;

// Draws the tray glyph (orange gauge) with an optional status dot, at sizePx x sizePx.
// usedFraction (0..1) greys the orange body from the left edge.
HICON MakeTrayIcon(DotColor dot, int sizePx, double usedFraction = 0.0);

// NIM_ADD + NOTIFYICON_VERSION_4; no NIF_SHOWTIP. usedFraction = current-session usage (0..1).
bool TrayAdd(HWND hwnd, DotColor dot, double usedFraction);
void TraySetIcon(HWND hwnd, DotColor dot, double usedFraction);  // NIM_MODIFY with a freshly drawn icon
void TrayRemove(HWND hwnd);
void ShowTrayMenu(HWND hwnd, POINT pt);    // Settings…, separator, Exit
RECT TrayIconRect(HWND hwnd);              // icon's screen rect; 1x1 at cursor if unknown
