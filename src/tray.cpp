#include "tray.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr UINT kIconId = 1;
HICON g_icon = nullptr;
DotColor g_dot = DotColor::None;

struct Rgba { float r, g, b, a; };

// Straight-alpha "over" compositing.
Rgba Over(Rgba top, Rgba bottom) {
  float a = top.a + bottom.a * (1 - top.a);
  if (a <= 0) return {0, 0, 0, 0};
  auto mix = [&](float t, float b) { return (t * top.a + b * bottom.a * (1 - top.a)) / a; };
  return {mix(top.r, bottom.r), mix(top.g, bottom.g), mix(top.b, bottom.b), a};
}

constexpr float kPi = 3.14159265f;

// Color of the glyph at normalized point (x, y) in [0,1]^2, before antialiasing.
Rgba Sample(float x, float y, DotColor dot) {
  const Rgba orange{0xD9 / 255.f, 0x77 / 255.f, 0x57 / 255.f, 1};
  const Rgba white{1, 1, 1, 1};
  Rgba c{0, 0, 0, 0};
  const float dx = x - 0.5f, dy = y - 0.5f;
  const float d = std::sqrt(dx * dx + dy * dy);
  if (d <= 0.48f) c = orange;
  // Gauge: a 270-degree white arc open at the bottom, plus a needle and hub.
  float ang = std::atan2(dy, dx);              // y down: +pi/2 is straight down
  float fromBottom = std::fabs(ang - kPi / 2);  // distance from "down" direction
  if (fromBottom > kPi) fromBottom = 2 * kPi - fromBottom;
  if (d >= 0.26f && d <= 0.36f && fromBottom >= kPi / 4) c = Over(white, c);
  // Needle pointing up-right (about 60% of the gauge).
  const float nx = std::cos(-kPi / 4), ny = std::sin(-kPi / 4);
  const float t = dx * nx + dy * ny;
  const float perp = std::fabs(dx * ny - dy * nx);
  if (t >= 0 && t <= 0.30f && perp <= 0.045f) c = Over(white, c);
  if (d <= 0.08f) c = Over(white, c);
  if (dot != DotColor::None) {
    const Rgba fill = dot == DotColor::Red ? Rgba{0xE0 / 255.f, 0x30 / 255.f, 0x2A / 255.f, 1}
                                           : Rgba{0x80 / 255.f, 0x80 / 255.f, 0x80 / 255.f, 1};
    const float ex = x - 0.77f, ey = y - 0.77f;
    const float de = std::sqrt(ex * ex + ey * ey);
    if (de <= 0.23f) c = white;  // ring separating the dot from the glyph
    if (de <= 0.18f) c = fill;
  }
  return c;
}

void SetTip(NOTIFYICONDATAW& nid) {
  wcscpy_s(nid.szTip, L"Claude usage");
}

NOTIFYICONDATAW BaseData(HWND hwnd) {
  NOTIFYICONDATAW nid{};
  nid.cbSize = sizeof(nid);
  nid.hWnd = hwnd;
  nid.uID = kIconId;
  return nid;
}

int IconSize(HWND hwnd) {
  UINT dpi = GetDpiForWindow(hwnd);
  if (dpi == 0) dpi = GetDpiForSystem();
  return GetSystemMetricsForDpi(SM_CXSMICON, dpi);
}

void ReplaceIcon(HICON icon) {
  if (g_icon) DestroyIcon(g_icon);
  g_icon = icon;
}

}  // namespace

HICON MakeTrayIcon(DotColor dot, int sizePx) {
  sizePx = std::max(sizePx, 8);
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi);
  bi.bV5Width = sizePx;
  bi.bV5Height = -sizePx;  // top-down
  bi.bV5Planes = 1;
  bi.bV5BitCount = 32;
  bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000;
  bi.bV5GreenMask = 0x0000FF00;
  bi.bV5BlueMask = 0x000000FF;
  bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (!color) return nullptr;

  constexpr int kSs = 4;  // 4x4 supersampling
  auto* px = static_cast<uint32_t*>(bits);
  for (int y = 0; y < sizePx; ++y) {
    for (int x = 0; x < sizePx; ++x) {
      float r = 0, g = 0, b = 0, a = 0;
      for (int sy = 0; sy < kSs; ++sy) {
        for (int sx = 0; sx < kSs; ++sx) {
          Rgba s = Sample((x + (sx + 0.5f) / kSs) / sizePx, (y + (sy + 0.5f) / kSs) / sizePx, dot);
          r += s.r * s.a; g += s.g * s.a; b += s.b * s.a; a += s.a;  // premultiplied accumulate
        }
      }
      const float n = kSs * kSs;
      uint32_t A = static_cast<uint32_t>(std::lround(a / n * 255));
      uint32_t R = 0, G = 0, B = 0;
      if (a > 0) {  // back to straight alpha
        R = static_cast<uint32_t>(std::lround(r / a * 255));
        G = static_cast<uint32_t>(std::lround(g / a * 255));
        B = static_cast<uint32_t>(std::lround(b / a * 255));
      }
      px[y * sizePx + x] = (A << 24) | (R << 16) | (G << 8) | B;
    }
  }

  HBITMAP mask = CreateBitmap(sizePx, sizePx, 1, 1, nullptr);
  ICONINFO ii{};
  ii.fIcon = TRUE;
  ii.hbmColor = color;
  ii.hbmMask = mask;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(color);
  DeleteObject(mask);
  return icon;
}

bool TrayAdd(HWND hwnd, DotColor dot) {
  g_dot = dot;
  ReplaceIcon(MakeTrayIcon(dot, IconSize(hwnd)));
  NOTIFYICONDATAW nid = BaseData(hwnd);
  nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  nid.uCallbackMessage = WM_APP_TRAY;
  nid.hIcon = g_icon;
  SetTip(nid);
  Shell_NotifyIconW(NIM_DELETE, &nid);  // in case a stale one exists (e.g. after Explorer restart)
  if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;
  nid.uVersion = NOTIFYICON_VERSION_4;
  return Shell_NotifyIconW(NIM_SETVERSION, &nid) != FALSE;
}

void TraySetDot(HWND hwnd, DotColor dot) {
  g_dot = dot;
  ReplaceIcon(MakeTrayIcon(dot, IconSize(hwnd)));
  NOTIFYICONDATAW nid = BaseData(hwnd);
  nid.uFlags = NIF_ICON;
  nid.hIcon = g_icon;
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayRemove(HWND hwnd) {
  NOTIFYICONDATAW nid = BaseData(hwnd);
  Shell_NotifyIconW(NIM_DELETE, &nid);
  ReplaceIcon(nullptr);
}

void ShowTrayMenu(HWND hwnd, POINT pt) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"Settings…");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");
  SetForegroundWindow(hwnd);
  UINT flags = TPM_RIGHTBUTTON;
  flags |= GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
  TrackPopupMenuEx(menu, flags, pt.x, pt.y, hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);
}

RECT TrayIconRect(HWND hwnd) {
  NOTIFYICONIDENTIFIER id{};
  id.cbSize = sizeof(id);
  id.hWnd = hwnd;
  id.uID = kIconId;
  RECT r{};
  if (SUCCEEDED(Shell_NotifyIconGetRect(&id, &r)) && r.right > r.left) return r;
  POINT pt;
  GetCursorPos(&pt);
  return RECT{pt.x, pt.y, pt.x + 1, pt.y + 1};
}
