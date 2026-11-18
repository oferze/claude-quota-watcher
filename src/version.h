#pragma once
// The app version, shown in About and in the exe's file properties (app.rc).
// build.bat sets it from its version parameter (build.bat 1.2.3); otherwise these defaults apply.
// Plain macros only: app.rc includes this file too.

#ifndef APP_VERSION_MAJOR
#define APP_VERSION_MAJOR 1
#endif
#ifndef APP_VERSION_MINOR
#define APP_VERSION_MINOR 0
#endif
#ifndef APP_VERSION_PATCH
#define APP_VERSION_PATCH 0
#endif

#define APP_STRINGIZE_(x) #x
#define APP_STRINGIZE(x) APP_STRINGIZE_(x)
// "1.0.0" as one narrow string literal (the resource compiler can't join adjacent literals).
#define APP_VERSION_TEXT APP_STRINGIZE(APP_VERSION_MAJOR.APP_VERSION_MINOR.APP_VERSION_PATCH)
