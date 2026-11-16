#pragma once
#include <windows.h>

#include <functional>

#include "state.h"

// Settings window: shows login status and lets the user re-check. Single instance.
class SettingsWindow {
 public:
  SettingsWindow(HINSTANCE inst, const AppModel* model, std::function<void()> checkAgain);
  ~SettingsWindow();
  SettingsWindow(const SettingsWindow&) = delete;
  SettingsWindow& operator=(const SettingsWindow&) = delete;

  void Show();     // creates or re-activates
  void Refresh();  // model changed
  HWND hwnd() const { return hwnd_; }  // for IsDialogMessage in the message loop

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT, WPARAM, LPARAM);
  void Create();
  void Layout();
  int Px(int dip) const { return MulDiv(dip, static_cast<int>(dpi_), 96); }

  HINSTANCE inst_;
  const AppModel* model_;
  std::function<void()> checkAgain_;
  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  HFONT font_ = nullptr;
  HWND labels_[5]{};
  HWND values_[5]{};
  HWND instructions_ = nullptr;
  HWND checkBtn_ = nullptr;
  HWND closeBtn_ = nullptr;
};
