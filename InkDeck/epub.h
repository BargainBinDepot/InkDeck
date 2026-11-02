#pragma once
// =====================================================================
//  EPUB -> eReader book, on the device (the eReader's "Get books").
//  Unzips the book (its own small inflate), follows the reading order in
//  the OPF and turns each XHTML page into text: paragraphs on their own
//  lines, a chapter marker (\x01title\x01) for every h1-h3 heading (a
//  heading straight after another joins it: "CHAPTER I The Beginning"),
//  Project Gutenberg's licence pages left out, accents as plain ASCII.
//  With pictures (JPEG and PNG, see picture.h), each becomes a BMP in
//  <book>.img/ and a \x02img 001.bmp W H\x02 line, as the browser converter
//  makes them. Reports to Progress while it works.
// =====================================================================
#include <Arduino.h>
#include "picture.h"

namespace Epub {

bool convert(const String& src, const String& dst, const String& title, const String& author,
             String& err, uint32_t* chapters = nullptr,
             const Picture::Style* pictures = nullptr, uint32_t* pictureCount = nullptr);

// Raw DEFLATE (zip method 8) into a buffer of known size. false if the data is bad.
bool inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);

}
