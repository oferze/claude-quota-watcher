#pragma once
#include <windows.h>

#include <functional>

#include "popup_view.h"
#include "state.h"

// The usage popup shown when hovering (or clicking) the tray icon.
class Popup {
 public:
  Popup(HINSTANCE inst, const AppModel* model, std::function<void()> openSettings);
  ~Popup();
  Popup(const Popup&) = delete;
  Popup& operator=(const Popup&) = delete;

  void ShowHover(RECT anchor);     // shown without activation; hides ~400 ms after the mouse leaves
  void TogglePinned(RECT anchor);  // stays until 2nd click, Esc, click elsewhere, or the button
  void OnIconHoverEnd();           // NIN_POPUPCLOSE
  void Refresh();                  // model changed
  void Hide();
  bool IsVisible() const;

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT, WPARAM, LPARAM);
  void Show(RECT anchor, bool pinned);
  void Place();
  int Render(HDC hdc, bool draw);  // returns content height in px
  void RebuildFonts(UINT dpi);
  int Px(int dip) const { return MulDiv(dip, static_cast<int>(dpi_), 96); }
  void Activate(bool on);

  HINSTANCE inst_;
  const AppModel* model_;
  std::function<void()> openSettings_;
  HWND hwnd_ = nullptr;
  RECT anchor_{};
  bool pinned_ = false;
  ULONGLONG leaveTick_ = 0;
  ULONGLONG lastDeactivateHide_ = 0;
  UINT dpi_ = 96;
  HFONT font_ = nullptr, bold_ = nullptr;
  RECT buttonRc_{}, linkRc_{};
  int hot_ = 0;  // 0 none, 1 button, 2 link
  PopupView view_;
};
