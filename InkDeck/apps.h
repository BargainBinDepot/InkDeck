#pragma once
// =====================================================================
//  App framework
//  Native apps are C++ callbacks; Lua apps from /apps are added to the
//  same list at runtime and dispatched through LuaHost.
// =====================================================================
#include <Arduino.h>
#include "screen.h"

struct App {
  const char* name;
  const char* icon;              // 1-3 ASCII chars drawn inside the icon box
  void (*onEnter)();             // optional, runs when the app opens
  void (*onKey)(uint8_t key);    // one keypress (Esc goes to onBack first)
  void (*onDraw)();              // draw the body area (status bar is drawn by the OS)
  bool (*onBack)();              // optional: true = Esc handled inside the app, false/none = go home
  void (*onTick)();              // optional: called every loop while the app is open
  int  lua;                      // -1 = native app, otherwise index into the Lua app list
  const uint8_t* iconImg;        // built-in apps: 32x32 1-bit icon (nullptr = none)
  const char* about;             // built-in apps: one-line description for the info popup
};

// One clipboard shared by every app (kept in RAM until power-off)
namespace Clipboard {
  void set(const String& text);
  const String& get();
}

namespace AppMgr {
  void begin();
  void handleKey(uint8_t key);
  void tick();                                   // runs app ticks, redraws if something asked for it
  void requestRedraw(Refresh mode = Refresh::Partial);
  void setTitle(const String& title);            // status-bar title override (cleared on app switch)
  void goHome();
  int  luaAppCount();
}
