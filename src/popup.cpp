#include "popup.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>

namespace {

constexpr wchar_t kClass[] = L"ClaudeQuotaWatcher.Popup";
constexpr wchar_t kUsageUrl[] = L"https://claude.ai/new#settings/usage";
constexpr UINT_PTR kHoverTimer = 1;
constexpr ULONGLONG kHideDelayMs = 400;
constexpr int kWidthDip = 300;
constexpr int kPadDip = 16;

COLORREF Blend(COLORREF a, COLORREF b, int pctB) {
  auto mix = [&](int x, int y) { return (x * (100 - pctB) + y * pctB) / 100; };
  return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}

void FillRectColor(HDC hdc, const RECT& r, COLORREF c) {
  SetDCBrushColor(hdc, c);
  FillRect(hdc, &r, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

bool PtIn(const RECT& r, POINT p) { return PtInRect(&r, p) != FALSE; }

}  // namespace

Popup::Popup(HINSTANCE inst, const AppModel* model, std::function<void()> openSettings)
    : inst_(inst), model_(model), openSettings_(std::move(openSettings)) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_DROPSHADOW;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass, L"Claude usage",
                          WS_POPUP | WS_BORDER, 0, 0, 10, 10, nullptr, nullptr, inst, this);
  DWORD corner = 2;  // DWMWCP_ROUND (Windows 11; ignored elsewhere)
  DwmSetWindowAttribute(hwnd_, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof(corner));
}

Popup::~Popup() {
  if (hwnd_) DestroyWindow(hwnd_);
  if (font_) DeleteObject(font_);
  if (bold_) DeleteObject(bold_);
}

bool Popup::IsVisible() const { return hwnd_ && IsWindowVisible(hwnd_); }

void Popup::ShowHover(RECT anchor) {
  if (IsVisible() && pinned_) return;
  Show(anchor, false);
}

void Popup::TogglePinned(RECT anchor) {
  if (IsVisible() && pinned_) { Hide(); return; }
  // The click that pinned-closed us via focus loss arrives right after; don't reopen.
  if (GetTickCount64() - lastDeactivateHide_ < 300) return;
  Show(anchor, true);
}

void Popup::OnIconHoverEnd() {
  if (IsVisible() && !pinned_) SetTimer(hwnd_, kHoverTimer, 100, nullptr);
}

void Popup::Hide() {
  if (!hwnd_) return;
  KillTimer(hwnd_, kHoverTimer);
  ShowWindow(hwnd_, SW_HIDE);
  pinned_ = false;
  hot_ = 0;
}

void Popup::Refresh() {
  if (IsVisible()) Place();
}

void Popup::Activate(bool on) {
  LONG_PTR ex = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  ex = on ? (ex & ~WS_EX_NOACTIVATE) : (ex | WS_EX_NOACTIVATE);
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex);
}

void Popup::Show(RECT anchor, bool pinned) {
  anchor_ = anchor;
  pinned_ = pinned;
  leaveTick_ = 0;
  Activate(pinned);
  Place();
  if (pinned) {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    KillTimer(hwnd_, kHoverTimer);
  } else {
    ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    SetTimer(hwnd_, kHoverTimer, 100, nullptr);
  }
}

void Popup::RebuildFonts(UINT dpi) {
  NONCLIENTMETRICSW ncm{};
  ncm.cbSize = sizeof(ncm);
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
  if (font_) DeleteObject(font_);
  if (bold_) DeleteObject(bold_);
  font_ = CreateFontIndirectW(&ncm.lfMessageFont);
  LOGFONTW b = ncm.lfMessageFont;
  b.lfWeight = FW_SEMIBOLD;
  bold_ = CreateFontIndirectW(&b);
}

void Popup::Place() {
  HMONITOR mon = MonitorFromRect(&anchor_, MONITOR_DEFAULTTONEAREST);
  UINT dx = 96, dy = 96;
  if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy))) dx = 96;
  if (dx != dpi_ || !font_) { dpi_ = dx; RebuildFonts(dpi_); }
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);

  view_ = BuildPopupView(*model_, NowUnixMs() / 1000, LocalHhmm, UnixToLocal);
  HDC hdc = GetDC(hwnd_);
  int h = Render(hdc, false);
  ReleaseDC(hwnd_, hdc);

  RECT frame{0, 0, Px(kWidthDip), h};
  AdjustWindowRectExForDpi(&frame, WS_POPUP | WS_BORDER, FALSE, WS_EX_TOOLWINDOW, dpi_);
  SIZE size{frame.right - frame.left, frame.bottom - frame.top};
  POINT p = PlacePopup(anchor_, mi.rcWork, size);
  SetWindowPos(hwnd_, HWND_TOPMOST, p.x, p.y, size.cx, size.cy, SWP_NOACTIVATE);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

int Popup::Render(HDC hdc, bool draw) {
  const COLORREF bg = GetSysColor(COLOR_WINDOW);
  const COLORREF fg = GetSysColor(COLOR_WINDOWTEXT);
  const COLORREF grey = GetSysColor(COLOR_GRAYTEXT);
  const COLORREF track = Blend(bg, fg, 15);
  const COLORREF fill = GetSysColor(COLOR_HIGHLIGHT);
  const COLORREF red = RGB(0xC4, 0x2B, 0x1C);
  const COLORREF link = GetSysColor(COLOR_HOTLIGHT);

  const int pad = Px(kPadDip);
  const int width = Px(kWidthDip);
  const int inner = width - 2 * pad;
  SetBkMode(hdc, TRANSPARENT);

  auto textHeight = [&](HFONT f) {
    SelectObject(hdc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    return static_cast<int>(tm.tmHeight);
  };
  auto text = [&](const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt) {
    if (!draw) return;
    SelectObject(hdc, f);
    SetTextColor(hdc, c);
    DrawTextW(hdc, s.c_str(), static_cast<int>(s.size()), &r, fmt | DT_NOPREFIX);
  };

  int y = pad;
  const int lineH = textHeight(font_);
  const int titleH = textHeight(bold_);

  // Title + plan.
  text(L"Claude usage", RECT{pad, y, pad + inner, y + titleH}, bold_, fg, DT_LEFT | DT_SINGLELINE);
  if (!view_.plan.empty()) text(view_.plan, RECT{pad, y, pad + inner, y + titleH}, font_, grey, DT_RIGHT | DT_SINGLELINE);
  y += titleH + Px(12);

  // Rows.
  for (const PopupRowView& row : view_.rows) {
    text(row.label, RECT{pad, y, pad + inner, y + lineH}, font_, fg, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    text(row.percent, RECT{pad, y, pad + inner, y + lineH}, bold_, fg, DT_RIGHT | DT_SINGLELINE);
    y += lineH + Px(4);
    const int barH = Px(6);
    if (draw) {
      FillRectColor(hdc, RECT{pad, y, pad + inner, y + barH}, track);
      const int w = static_cast<int>(inner * row.fraction + 0.5);
      if (w > 0) FillRectColor(hdc, RECT{pad, y, pad + w, y + barH}, fill);
    }
    y += barH + Px(4);
    if (!row.reset.empty()) {
      text(row.reset, RECT{pad, y, pad + inner, y + lineH}, font_, grey, DT_LEFT | DT_SINGLELINE);
      y += lineH;
    }
    y += Px(12);
  }

  // Status (may wrap).
  {
    SelectObject(hdc, font_);
    RECT r{pad, y, pad + inner, y};
    DrawTextW(hdc, view_.status.c_str(), static_cast<int>(view_.status.size()), &r,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    const int h = std::max(static_cast<int>(r.bottom - r.top), lineH);
    text(view_.status, RECT{pad, y, pad + inner, y + h}, font_, view_.statusIsError ? red : grey,
         DT_LEFT | DT_WORDBREAK);
    y += h + Px(12);
  }

  // Footer: optional "Settings…" link (left) and the button (right).
  {
    SelectObject(hdc, font_);
    const std::wstring label = L"Open usage page";
    SIZE ts{};
    GetTextExtentPoint32W(hdc, label.c_str(), static_cast<int>(label.size()), &ts);
    const int bh = std::max(Px(28), static_cast<int>(ts.cy) + Px(10));
    const int bw = ts.cx + Px(24);
    buttonRc_ = RECT{pad + inner - bw, y, pad + inner, y + bh};
    if (draw) {
      SetDCBrushColor(hdc, hot_ == 1 ? Blend(GetSysColor(COLOR_BTNFACE), fill, 15) : GetSysColor(COLOR_BTNFACE));
      SetDCPenColor(hdc, Blend(bg, fg, 35));
      SelectObject(hdc, GetStockObject(DC_BRUSH));
      SelectObject(hdc, GetStockObject(DC_PEN));
      RoundRect(hdc, buttonRc_.left, buttonRc_.top, buttonRc_.right, buttonRc_.bottom, Px(8), Px(8));
    }
    text(label, buttonRc_, font_, GetSysColor(COLOR_BTNTEXT), DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    linkRc_ = RECT{};
    if (view_.showSettingsLink) {
      const std::wstring s = L"Settings…";
      SIZE ls{};
      GetTextExtentPoint32W(hdc, s.c_str(), static_cast<int>(s.size()), &ls);
      const int ly = y + (bh - ls.cy) / 2;
      linkRc_ = RECT{pad, ly, pad + ls.cx, ly + ls.cy};
      if (draw) {
        LOGFONTW lf;
        GetObjectW(font_, sizeof(lf), &lf);
        lf.lfUnderline = hot_ == 2;
        HFONT u = CreateFontIndirectW(&lf);
        text(s, linkRc_, u, link, DT_LEFT | DT_SINGLELINE);
        SelectObject(hdc, font_);
        DeleteObject(u);
      }
    }
    y += bh + pad;
  }
  return y;
}

LRESULT CALLBACK Popup::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  }
  auto* self = reinterpret_cast<Popup*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self && self->hwnd_ == hwnd) return self->Handle(msg, wp, lp);
  return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Popup::Handle(UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC hdc = BeginPaint(hwnd_, &ps);
      RECT rc;
      GetClientRect(hwnd_, &rc);
      HDC mem = CreateCompatibleDC(hdc);
      HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
      HGDIOBJ oldBmp = SelectObject(mem, bmp);
      HGDIOBJ oldFont = SelectObject(mem, font_);
      FillRectColor(mem, rc, GetSysColor(COLOR_WINDOW));
      Render(mem, true);
      BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
      SelectObject(mem, oldFont);
      SelectObject(mem, oldBmp);
      DeleteObject(bmp);
      DeleteDC(mem);
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_TIMER:
      if (wp == kHoverTimer && !pinned_) {
        POINT pt;
        GetCursorPos(&pt);
        RECT wr;
        GetWindowRect(hwnd_, &wr);
        RECT icon = anchor_;
        InflateRect(&icon, 2, 2);
        if (PtIn(wr, pt) || PtIn(icon, pt)) {
          leaveTick_ = 0;
        } else if (leaveTick_ == 0) {
          leaveTick_ = GetTickCount64();
        } else if (GetTickCount64() - leaveTick_ >= kHideDelayMs) {
          Hide();
        }
      }
      return 0;
    case WM_MOUSEACTIVATE:
      return pinned_ ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE && pinned_) {
        Hide();
        lastDeactivateHide_ = GetTickCount64();
      }
      return 0;
    case WM_KEYDOWN:
      if (wp == VK_ESCAPE) Hide();
      return 0;
    case WM_MOUSEMOVE: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      int hot = PtIn(buttonRc_, pt) ? 1 : (view_.showSettingsLink && PtIn(linkRc_, pt)) ? 2 : 0;
      if (hot != hot_) {
        hot_ = hot;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
      TrackMouseEvent(&tme);
      return 0;
    }
    case WM_MOUSELEAVE:
      if (hot_) { hot_ = 0; InvalidateRect(hwnd_, nullptr, FALSE); }
      return 0;
    case WM_SETCURSOR:
      if (hot_) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
      break;
    case WM_LBUTTONUP: {
      POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
      if (PtIn(buttonRc_, pt)) {
        Hide();
        ShellExecuteW(nullptr, L"open", kUsageUrl, nullptr, nullptr, SW_SHOWNORMAL);
      } else if (view_.showSettingsLink && PtIn(linkRc_, pt)) {
        Hide();
        if (openSettings_) openSettings_();
      }
      return 0;
    }
    case WM_DPICHANGED:
      Place();
      return 0;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
      if (font_) { DeleteObject(font_); font_ = nullptr; }
      if (IsVisible()) Place();
      return 0;
  }
  return DefWindowProcW(hwnd_, msg, wp, lp);
}
