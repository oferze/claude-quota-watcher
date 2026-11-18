#include "about.h"

#include <commctrl.h>
#include <shellapi.h>

#include <string>

#include "tray.h"
#include "version.h"

namespace {

constexpr wchar_t kClass[] = L"ClaudeQuotaWatcher.About";
constexpr wchar_t kTitle[] = L"About Claude Quota Watcher";
constexpr wchar_t kReleasesUrl[] = L"https://github.com/oferze/claude-quota-watcher/releases";
constexpr int kWidthDip = 380;
constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;

}  // namespace

AboutWindow::AboutWindow(HINSTANCE inst) : inst_(inst) {
  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_LINK_CLASS};
  InitCommonControlsEx(&icc);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
}

AboutWindow::~AboutWindow() {
  if (hwnd_) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
  if (titleFont_) DeleteObject(titleFont_);
  if (iconImage_) DestroyIcon(iconImage_);
}

void AboutWindow::Show() {
  if (!hwnd_) Create();
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);
}

void AboutWindow::Create() {
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
  icon_ = make(L"STATIC", nullptr, SS_ICON | SS_REALSIZECONTROL);
  title_ = make(L"STATIC", L"Claude Quota Watcher", SS_LEFT | SS_NOPREFIX);
  version_ = make(L"STATIC", L"Version " APP_VERSION_TEXT, SS_LEFT | SS_NOPREFIX);
  copyright_ = make(L"STATIC", L"© 2026 Ofer Zelig · MIT License", SS_LEFT | SS_NOPREFIX);
  note_ = make(L"STATIC", L"Unofficial: not affiliated with or endorsed by Anthropic.", SS_LEFT | SS_NOPREFIX);
  const std::wstring link = std::wstring(L"<a href=\"") + kReleasesUrl + L"\">Check for Updates</a>";
  link_ = make(WC_LINK, link.c_str(), WS_TABSTOP);
  okBtn_ = make(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
  Layout();

  // Center on the work area.
  RECT wr;
  GetWindowRect(hwnd_, &wr);
  const int w = wr.right - wr.left, h = wr.bottom - wr.top;
  SetWindowPos(hwnd_, nullptr, (mi.rcWork.left + mi.rcWork.right - w) / 2, (mi.rcWork.top + mi.rcWork.bottom - h) / 2,
               0, 0, SWP_NOSIZE | SWP_NOZORDER);
  SetFocus(okBtn_);
}

void AboutWindow::Layout() {
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi_);
  if (font_) DeleteObject(font_);
  if (titleFont_) DeleteObject(titleFont_);
  font_ = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW big = ncm.lfMessageFont;
  big.lfHeight = big.lfHeight * 4 / 3;
  big.lfWeight = FW_SEMIBOLD;
  titleFont_ = CreateFontIndirectW(&big);
  for (HWND c : {version_, copyright_, note_, link_, okBtn_}) {
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
  }
  SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);

  // The tray ring, about a third full, as the window's picture.
  const int iconPx = Px(32);
  HICON old = iconImage_;
  iconImage_ = MakeTrayIcon(DotColor::None, iconPx, 0.3);
  SendMessageW(icon_, STM_SETICON, reinterpret_cast<WPARAM>(iconImage_), 0);
  if (old) DestroyIcon(old);

  const int pad = Px(16), width = Px(kWidthDip), lineH = Px(20);
  const int textX = pad + iconPx + Px(14), textW = width - textX - pad;
  MoveWindow(icon_, pad, pad + Px(2), iconPx, iconPx, TRUE);
  int y = pad;
  MoveWindow(title_, textX, y, textW, Px(26), TRUE);
  y += Px(28);
  MoveWindow(version_, textX, y, textW, lineH, TRUE);
  y += lineH;
  MoveWindow(copyright_, textX, y, textW, lineH, TRUE);
  y += lineH + Px(10);
  // The note may wrap at large text sizes: measure it.
  wchar_t note[128] = L"";
  GetWindowTextW(note_, note, 128);
  RECT noteRect{0, 0, textW, 0};
  HDC dc = GetDC(note_);
  HGDIOBJ oldFont = SelectObject(dc, font_);
  DrawTextW(dc, note, -1, &noteRect, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  SelectObject(dc, oldFont);
  ReleaseDC(note_, dc);
  const int noteH = noteRect.bottom > lineH ? noteRect.bottom : lineH;
  MoveWindow(note_, textX, y, textW, noteH, TRUE);
  y += noteH + Px(8);
  MoveWindow(link_, textX, y, textW, lineH, TRUE);
  y += lineH + Px(12);
  const int bw = Px(88), bh = Px(28);
  MoveWindow(okBtn_, width - pad - bw, y, bw, bh, TRUE);
  y += bh + pad;

  RECT frame{0, 0, width, y};
  AdjustWindowRectExForDpi(&frame, kStyle, FALSE, WS_EX_DLGMODALFRAME, dpi_);
  SetWindowPos(hwnd_, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK AboutWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<AboutWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_ == hwnd) return self->Handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT AboutWindow::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_COMMAND:
      if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) ShowWindow(hwnd_, SW_HIDE);
      return 0;
    case WM_NOTIFY: {
      const auto* hdr = reinterpret_cast<const NMHDR*>(lp);
      if (hdr->hwndFrom == link_ && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
        const auto* link = reinterpret_cast<const NMLINK*>(lp);
        ShellExecuteW(nullptr, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      ShowWindow(hwnd_, SW_HIDE);
      return 0;
    case WM_CTLCOLORSTATIC:
      if (reinterpret_cast<HWND>(lp) != note_) break;
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
