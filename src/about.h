#pragma once
#include <windows.h>

// About window: app name, version, copyright, the unofficial note and a Check for Updates link. Single instance.
class AboutWindow {
 public:
  explicit AboutWindow(HINSTANCE inst);
  ~AboutWindow();
  AboutWindow(const AboutWindow&) = delete;
  AboutWindow& operator=(const AboutWindow&) = delete;

  void Show();  // creates or re-activates
  HWND hwnd() const { return hwnd_; }  // for IsDialogMessage in the message loop

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT Handle(UINT, WPARAM, LPARAM);
  void Create();
  void Layout();
  int Px(int dip) const { return MulDiv(dip, static_cast<int>(dpi_), 96); }

  HINSTANCE inst_;
  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  HFONT font_ = nullptr;
  HFONT titleFont_ = nullptr;
  HICON iconImage_ = nullptr;
  HWND icon_ = nullptr;
  HWND title_ = nullptr;
  HWND version_ = nullptr;
  HWND copyright_ = nullptr;
  HWND note_ = nullptr;
  HWND link_ = nullptr;
  HWND okBtn_ = nullptr;
};
