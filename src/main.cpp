// Claude Quota Watcher — tray app showing Claude plan usage.
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <memory>

#include "poller.h"
#include "popup.h"
#include "state.h"
#include "tray.h"

namespace {

constexpr UINT_PTR kPollTimer = 1;
constexpr UINT kPollIntervalMs = 60000;
constexpr wchar_t kWindowClass[] = L"ClaudeQuotaWatcher.Hidden";

struct App {
  HINSTANCE inst = nullptr;
  HWND hwnd = nullptr;
  AppModel model;
  std::unique_ptr<Poller> poller;
  int64_t retryNotBeforeMs = 0;
  UINT taskbarCreatedMsg = 0;
  std::unique_ptr<Popup> popup;
  bool previewPending = false;  // --preview: pin the popup after the first result (for screenshots)
};
App g_app;

void OnPollResult(PollResult* raw) {
  std::unique_ptr<PollResult> r(raw);
  AppModel& m = g_app.model;
  ApplyCredResult(m, r->action, r->creds);
  if (r->classified) ApplyClassified(m, *r->classified, NowUnixMs() / 1000);
  if (r->retryAfterSec) g_app.retryNotBeforeMs = NowUnixMs() + int64_t{*r->retryAfterSec} * 1000;
  TraySetDot(g_app.hwnd, DotFor(m.state));
  g_app.poller->OnResultHandled(*r);
  g_app.popup->Refresh();
  if (g_app.previewPending) {
    g_app.previewPending = false;
    g_app.popup->TogglePinned(TrayIconRect(g_app.hwnd));
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g_app.taskbarCreatedMsg && msg != 0) {
    TrayAdd(hwnd, DotFor(g_app.model.state));
    return 0;
  }
  switch (msg) {
    case WM_APP_POLLRESULT:
      OnPollResult(reinterpret_cast<PollResult*>(lp));
      return 0;
    case WM_TIMER:
      if (wp == kPollTimer) g_app.poller->PollNow(g_app.retryNotBeforeMs);
      return 0;
    case WM_APP_TRAY:
      switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
          g_app.popup->Hide();
          ShowTrayMenu(hwnd, POINT{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)});
          break;
        case NIN_POPUPOPEN:
          g_app.popup->ShowHover(TrayIconRect(hwnd));
          break;
        case NIN_POPUPCLOSE:
          g_app.popup->OnIconHoverEnd();
          break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
          g_app.popup->TogglePinned(TrayIconRect(hwnd));
          break;
      }
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDM_EXIT:
          DestroyWindow(hwnd);
          break;
      }
      return 0;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      TraySetDot(hwnd, DotFor(g_app.model.state));  // redraw icon at the current size
      return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_DESTROY:
      KillTimer(hwnd, kPollTimer);
      g_app.popup.reset();
      TrayRemove(hwnd);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ClaudeQuotaWatcher.SingleInstance");
  if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) return 0;

  g_app.inst = inst;
  g_app.taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kWindowClass;
  RegisterClassExW(&wc);
  // A hidden top-level window (not HWND_MESSAGE) so it receives TaskbarCreated broadcasts.
  g_app.hwnd = CreateWindowExW(0, kWindowClass, L"Claude Quota Watcher", WS_OVERLAPPED, 0, 0, 0, 0, nullptr,
                               nullptr, inst, nullptr);
  if (!g_app.hwnd) return 1;
  ChangeWindowMessageFilterEx(g_app.hwnd, g_app.taskbarCreatedMsg, MSGFLT_ALLOW, nullptr);

  g_app.poller = std::make_unique<Poller>(g_app.hwnd);
  g_app.popup = std::make_unique<Popup>(inst, &g_app.model,
                                        [] { PostMessageW(g_app.hwnd, WM_COMMAND, IDM_SETTINGS, 0); });
  g_app.previewPending = wcsstr(GetCommandLineW(), L"--preview") != nullptr;
  TrayAdd(g_app.hwnd, DotFor(g_app.model.state));
  g_app.poller->PollNow(0);
  SetTimer(g_app.hwnd, kPollTimer, kPollIntervalMs, nullptr);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
