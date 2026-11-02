#pragma once
// =====================================================================
//  Net — internet access for apps (the eReader's "Get books").
//  Joins the WiFi saved in /system/wifi.txt (or uses the Uploader's, if it
//  is connected), fetches pages over HTTPS and saves downloads to the SD.
//  Everything blocks while it runs, reading the keyboard meanwhile.
//
//  Gutenberg::convert() turns a Project Gutenberg plain-text book into the
//  eReader's format: licence header and footer removed, hard-wrapped lines
//  joined into paragraphs, chapter headings marked, accents to plain ASCII.
// =====================================================================
#include <Arduino.h>

namespace Net {

bool connect(String& err);              // join the saved WiFi (up to ~15 s); true if online
void disconnect();                      // our WiFi off again (leaves the Uploader's alone)
bool online();
bool busy();                            // WiFi on for an app (battery readings wait)

// GET a page into `out` (at most maxBytes). err says why on failure.
bool get(const String& url, String& out, size_t maxBytes, String& err);
// GET into a file on the SD; returns bytes written, or -1 (err says why)
long download(const String& url, const String& path, String& err);

}

namespace Gutenberg {

// src: the downloaded .txt; dst: the book for the eReader. title/author go on top.
bool convert(const String& src, const String& dst, const String& title, const String& author,
             String& err, uint32_t* chapters = nullptr);

}
