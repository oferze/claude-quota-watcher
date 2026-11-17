// Claude Quota Watcher — tray app showing Claude plan usage.
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>

#include <memory>

#include "poller.h"
#include "popup.h"
#include "settings.h"
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
  std::unique_ptr<SettingsWindow> settings;
  bool previewPending = false;  // --preview: pin the popup after the first result (for screenshots)
  HPOWERNOTIFY displayNotify = nullptr;
  bool displayOff = false;   // Windows turned the screen off (idle timeout, sleep): no polling
  int64_t lastCheckMs = 0;   // last time the user hovered/clicked the tray icon (GetTickCount64)
  int64_t lastPollMs = 0;
};
App g_app;

int64_t TickMs() { return static_cast<int64_t>(GetTickCount64()); }

void PollNow(int64_t retryNotBeforeMs) {
  g_app.lastPollMs = TickMs();
  g_app.poller->PollNow(retryNotBeforeMs);
}

// The user looked at the popup: back to polling every minute, and refresh now if we'd slowed down.
void OnUserCheck() {
  const int64_t now = TickMs();
  const bool wasSlow = PollIntervalMs(now, g_app.lastCheckMs) > PollIntervalMs(now, now);
  g_app.lastCheckMs = now;
  if (wasSlow) PollNow(g_app.retryNotBeforeMs);
}

// GUID_CONSOLE_DISPLAY_STATE: 0 = off, 1 = on, 2 = dimmed. Stop while off; poll as soon as it's back on.
void OnDisplayState(DWORD state) {
  const bool off = state == 0;
  if (g_app.displayOff && !off) PollNow(g_app.retryNotBeforeMs);
  g_app.displayOff = off;
}

void OnPollResult(PollResult* raw) {
  std::unique_ptr<PollResult> r(raw);
  AppModel& m = g_app.model;
  ApplyCredResult(m, r->action, r->creds);
  if (r->classified) ApplyClassified(m, *r->classified, NowUnixMs() / 1000);
  if (r->retryAfterSec) g_app.retryNotBeforeMs = NowUnixMs() + int64_t{*r->retryAfterSec} * 1000;
  TraySetIcon(g_app.hwnd, DotFor(m.state), SessionFraction(m));
  g_app.poller->OnResultHandled(*r);
  g_app.popup->Refresh();
  g_app.settings->Refresh();
  if (g_app.previewPending) {
    g_app.previewPending = false;
    g_app.popup->TogglePinned(TrayIconRect(g_app.hwnd));
  }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == g_app.taskbarCreatedMsg && msg != 0) {
    TrayAdd(hwnd, DotFor(g_app.model.state), SessionFraction(g_app.model));
    return 0;
  }
  switch (msg) {
    case WM_APP_POLLRESULT:
      OnPollResult(reinterpret_cast<PollResult*>(lp));
      return 0;
    case WM_TIMER:
      if (wp == kPollTimer && !g_app.displayOff && PollDue(TickMs(), g_app.lastCheckMs, g_app.lastPollMs)) {
        PollNow(g_app.retryNotBeforeMs);
      }
      return 0;
    case WM_POWERBROADCAST:
      if (wp == PBT_POWERSETTINGCHANGE) {
        const auto* s = reinterpret_cast<const POWERBROADCAST_SETTING*>(lp);
        if (s->PowerSetting == GUID_CONSOLE_DISPLAY_STATE && s->DataLength >= sizeof(DWORD)) {
          OnDisplayState(*reinterpret_cast<const DWORD*>(s->Data));
        }
      }
      return TRUE;
    case WM_APP_TRAY:
      switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
          g_app.popup->Hide();
          ShowTrayMenu(hwnd, POINT{GET_X_LPARAM(wp), GET_Y_LPARAM(wp)});
          break;
        case NIN_POPUPOPEN:
          OnUserCheck();
          g_app.popup->ShowHover(TrayIconRect(hwnd));
          break;
        case NIN_POPUPCLOSE:
          g_app.popup->OnIconHoverEnd();
          break;
        case NIN_SELECT:
        case NIN_KEYSELECT:
          OnUserCheck();
          g_app.popup->TogglePinned(TrayIconRect(hwnd));
          break;
      }
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wp)) {
        case IDM_SETTINGS:
          g_app.popup->Hide();
          g_app.settings->Show();
          break;
        case IDM_EXIT:
          DestroyWindow(hwnd);
          break;
      }
      return 0;
    case WM_SETTINGCHANGE:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
      TraySetIcon(hwnd, DotFor(g_app.model.state), SessionFraction(g_app.model));  // redraw icon at the current size
      return DefWindowProcW(hwnd, msg, wp, lp);
    case WM_DESTROY:
      KillTimer(hwnd, kPollTimer);
      if (g_app.displayNotify) UnregisterPowerSettingNotification(g_app.displayNotify);
      g_app.popup.reset();
      g_app.settings.reset();
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
  // "Check again" re-reads the login and polls now, ignoring any Retry-After wait.
  g_app.settings = std::make_unique<SettingsWindow>(inst, &g_app.model, [] { PollNow(0); });
  g_app.previewPending = wcsstr(GetCommandLineW(), L"--preview") != nullptr;
  TrayAdd(g_app.hwnd, DotFor(g_app.model.state), SessionFraction(g_app.model));
  g_app.lastCheckMs = TickMs();  // starting the app counts as a check: 30 minutes of 1-minute polling
  PollNow(0);
  SetTimer(g_app.hwnd, kPollTimer, kPollIntervalMs, nullptr);
  // Screen off/on notifications (also covers sleep: the screen turns off before and on after).
  g_app.displayNotify =
      RegisterPowerSettingNotification(g_app.hwnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
  if (wcsstr(GetCommandLineW(), L"--settings")) g_app.settings->Show();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    HWND sw = g_app.settings ? g_app.settings->hwnd() : nullptr;
    if (sw && IsDialogMessageW(sw, &msg)) continue;  // Tab / Enter / Esc in the Settings window
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  ReleaseMutex(mutex);
  CloseHandle(mutex);
  return 0;
}
