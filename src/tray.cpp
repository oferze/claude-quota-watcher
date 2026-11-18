#include "tray.h"

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

constexpr UINT kIconId = 1;
HICON g_icon = nullptr;

struct Rgba { float r, g, b, a; };

constexpr float kPi = 3.14159265f;

constexpr Rgba Hex(uint32_t rgb, float a = 1) {
  return {((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a};
}

// Arc colour by session usage: orange, amber from 70%, red from 90%.
Rgba ArcColor(float used) {
  return used >= 0.9f ? Hex(0xD93B30) : used >= 0.7f ? Hex(0xE8A33D) : Hex(0xD97757);
}

// Color of the glyph at normalized point (x, y) in [0,1]^2, before antialiasing.
// A progress ring: the arc fills clockwise from 12 o'clock to usedFraction, over a see-through grey track.
Rgba Sample(float x, float y, DotColor dot, float usedFraction) {
  const Rgba white{1, 1, 1, 1};
  Rgba c{0, 0, 0, 0};
  const float dx = x - 0.5f, dy = y - 0.5f;
  const float d = std::sqrt(dx * dx + dy * dy);
  if (d >= 0.26f && d <= 0.48f) {
    float turn = std::atan2(dx, -dy) / (2 * kPi);  // 0 at 12 o'clock, clockwise (y points down)
    if (turn < 0) turn += 1;
    c = turn < usedFraction ? ArcColor(usedFraction) : Hex(0x8A8A8A, 120 / 255.f);
  }
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

HICON MakeTrayIcon(DotColor dot, int sizePx, double usedFraction) {
  sizePx = std::max(sizePx, 8);
  const float used = static_cast<float>(std::clamp(usedFraction, 0.0, 1.0));
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
          Rgba s = Sample((x + (sx + 0.5f) / kSs) / sizePx, (y + (sy + 0.5f) / kSs) / sizePx, dot, used);
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

bool TrayAdd(HWND hwnd, DotColor dot, double usedFraction) {
  ReplaceIcon(MakeTrayIcon(dot, IconSize(hwnd), usedFraction));
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

void TraySetIcon(HWND hwnd, DotColor dot, double usedFraction) {
  ReplaceIcon(MakeTrayIcon(dot, IconSize(hwnd), usedFraction));
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
  AppendMenuW(menu, MF_STRING, IDM_ABOUT, L"About…");
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
