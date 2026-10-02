#pragma once
// =====================================================================
//  LuaHost — runs one Lua app at a time.
//  An app is a folder /apps/<id>/ containing:
//    app.ini   name=, icon=, image= (default icon.bmp), entry= (default main.lua),
//              version= / author= / description= (shown in the info popup),
//              optional tab.label= / tab.dir= / tab.accept= / tab.page= / tab.editor=
//    main.lua  defines any of: init(), key(k, ch), draw(), back(), tick()
//  Full API reference: APP_API.md
// =====================================================================
#include <Arduino.h>
#include <vector>

namespace LuaHost {

struct AppInfo {
  String id;         // folder name
  String name;       // launcher label
  String icon;       // 1-3 chars, shown when there's no icon image
  String image;      // icon image file in the app folder (default icon.bmp)
  String version;    // optional metadata shown in the info popup
  String author;
  String description;
  String entry;      // script file, default main.lua
  String tabLabel;   // web uploader tab (optional)
  String tabDir;     // folder inside the app the tab manages
  String tabAccept;  // file types for the tab's upload box, e.g. ".txt,.md"
  String tabPage;    // optional: the app's own web page (file in the app folder) used as its tab
  String tabEditor;  // optional: "text" or "markdown" = the tab is a browser editor for tab.dir
};

std::vector<AppInfo> readManifests();   // scans /apps/*/app.ini, sorted by name

bool open(const AppInfo& app);          // start an app; errors are shown on screen
void close();                           // stop it and free all its memory
void key(uint8_t k);
void draw();
bool back();                            // true = app handled Esc itself
void tick();
bool exitRequested();                   // app called sys.exit()
size_t memUsed();

}
