#include "settings.h"

#include "credentials.h"
#include "popup_view.h"

namespace {

constexpr wchar_t kClass[] = L"ClaudeQuotaWatcher.Settings";
constexpr wchar_t kTitle[] = L"Claude Quota Watcher — Settings";
constexpr int kCheckId = 100;
constexpr int kWidthDip = 500;
constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

std::wstring LocalDateTime(int64_t unixSec) {
  SYSTEMTIME st = UnixToLocal(unixSec);
  wchar_t date[64] = L"";
  GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &st, nullptr, date, 64, nullptr);
  return std::wstring(date) + L" " + LocalHhmm(unixSec);
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE inst, const AppModel* model, std::function<void()> checkAgain)
    : inst_(inst), model_(model), checkAgain_(std::move(checkAgain)) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
}

SettingsWindow::~SettingsWindow() {
  if (hwnd_) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
}

void SettingsWindow::Show() {
  if (!hwnd_) Create();
  Refresh();
  ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(hwnd_);
}

void SettingsWindow::Create() {
  // Open on the monitor under the cursor, at that monitor's DPI.
  POINT pt;
  GetCursorPos(&pt);
  HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);
  hwnd_ = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, kTitle, kStyle, mi.rcWork.left + 100, mi.rcWork.top + 100,
                          400, 300, nullptr, nullptr, inst_, this);
  dpi_ = GetDpiForWindow(hwnd_);

  auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id = 0) {
    return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, hwnd_,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst_, nullptr);
  };
  const wchar_t* names[5] = {L"Login:", L"Plan:", L"Token expires:", L"Credentials file:", L"Last result:"};
  for (int i = 0; i < 5; ++i) {
    labels_[i] = make(L"STATIC", names[i], SS_LEFT);
    values_[i] = make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX | (i == 3 ? SS_PATHELLIPSIS : SS_ENDELLIPSIS));
  }
  instructions_ = make(L"STATIC", kLoginInstructions, SS_LEFT | SS_NOPREFIX);
  checkBtn_ = make(L"BUTTON", L"Check again", WS_TABSTOP | BS_DEFPUSHBUTTON, kCheckId);
  closeBtn_ = make(L"BUTTON", L"Close", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
  Layout();

  // Center on the work area.
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  const int w = wr.right - wr.left, h = wr.bottom - wr.top;
  SetWindowPos(hwnd_, nullptr, (mi.rcWork.left + mi.rcWork.right - w) / 2, (mi.rcWork.top + mi.rcWork.bottom - h) / 2,
               0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

void SettingsWindow::Layout() {
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_);
  if (font_) DeleteObject(font_);
  font_ = CreateFontIndirectW(&ncm.lfMessageFont);
  for (HWND c : {labels_[0], labels_[1], labels_[2], labels_[3], labels_[4], values_[0], values_[1], values_[2],
                 values_[3], values_[4], instructions_, checkBtn_, closeBtn_}) {
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  }

  const int pad = Px(16), labelW = Px(120), lineH = Px(22), width = Px(kWidthDip);
  const int valueX = pad + labelW, valueW = width - valueX - pad;
  int y = pad;
  for (int i = 0; i < 5; ++i) {
    MoveWindow(labels_[i], pad, y, labelW, lineH, TRUE);
    MoveWindow(values_[i], valueX, y, valueW, lineH, TRUE);
    y += lineH;
  }
  y += Px(8);
  MoveWindow(instructions_, pad, y, width - 2 * pad, Px(40), TRUE);
  y += Px(40) + Px(12);
  const int bw = Px(96), bh = Px(28);
  MoveWindow(closeBtn_, width - pad - bw, y, bw, bh, TRUE);
  MoveWindow(checkBtn_, width - pad - 2 * bw - Px(8), y, bw, bh, TRUE);
  y += bh + pad;

  RECT frame{0, 0, width, y};
  AdjustWindowRectExForDpi(&frame, kStyle, FALSE, WS_EX_DLGMODALFRAME, dpi_);
  SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::Refresh() {
  if (!hwnd_) return;
  SettingsView v = BuildSettingsView(*model_, CredentialsPath(), LocalHhmm, LocalDateTime);
  const std::wstring* texts[5] = {&v.login, &v.plan, &v.tokenExpires, &v.path, &v.lastResult};
  for (int i = 0; i < 5; ++i) SetWindowTextW(values_[i], texts[i]->c_str());
  ShowWindow(instructions_, v.showInstructions ? SW_SHOW : SW_HIDE);
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_ == hwnd) return self->Handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wp) == kCheckId) {
        if (checkAgain_) checkAgain_();
        Refresh();
      } else if (LOWORD(wp) == IDCANCEL) {
        ShowWindow(hwnd_, SW_HIDE);
      }
      return 0;
    case WM_CLOSE:
      ShowWindow(hwnd_, SW_HIDE);
      return 0;
    case WM_CTLCOLORSTATIC:
      if (reinterpret_cast<HWND>(lp) != instructions_) break;
      SetTextColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_GRAYTEXT));
      SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_BTNFACE));
      return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
    case WM_DPICHANGED: {
      dpi_ = HIWORD(wp);
      const RECT* r = reinterpret_cast<const RECT*>(lp);
      SetWindowPos(hwnd_, nullptr, r->left, r->top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      Layout();
      return 0;
    }
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}
