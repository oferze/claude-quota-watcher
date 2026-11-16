#pragma once
#include <windows.h>

#include "state.h"

constexpr UINT WM_APP_TRAY = WM_APP + 2;  // tray callback; LOWORD(lParam) = event
constexpr UINT IDM_SETTINGS = 1001;
constexpr UINT IDM_EXIT = 1002;

// Draws the tray glyph (orange gauge) with an optional status dot, at sizePx x sizePx.
HICON MakeTrayIcon(DotColor dot, int sizePx);

bool TrayAdd(HWND hwnd, DotColor dot);     // NIM_ADD + NOTIFYICON_VERSION_4; no NIF_SHOWTIP
void TraySetDot(HWND hwnd, DotColor dot);  // NIM_MODIFY with a freshly drawn icon
void TrayRemove(HWND hwnd);
void ShowTrayMenu(HWND hwnd, POINT pt);    // Settings…, separator, Exit
RECT TrayIconRect(HWND hwnd);              // icon's screen rect; 1x1 at cursor if unknown
