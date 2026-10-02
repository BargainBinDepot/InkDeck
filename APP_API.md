# InkDeck app API (v0.17)

An app is a folder in `/apps` on the SD card:

```
/apps/myapp/
  app.ini      manifest
  main.lua     the app
  icon.bmp     optional home-screen icon
  ...          anything else the app reads or writes (its data lives here too)
```

Install one by copying the folder onto the card, or in the Uploader's **Apps** tab by picking the folder. The folder name becomes the app's id. Re-installing the same folder updates the app and keeps its data files.

## app.ini

```ini
name=Reader        # launcher label (keep it short, ~8 chars shows fully on the 3.7")
icon=Bk            # 1-3 characters, used when there's no icon image (default: first 2 letters of name)
image=icon.bmp     # optional, default icon.bmp
entry=main.lua     # optional, default main.lua

# Optional details shown in the info popup (press i on the home screen)
version=1.0
author=Your Name
description=One or two sentences about what the app does.

# Optional: add a tab to the web uploader that manages a folder inside the app
tab.label=Books
tab.dir=books              # -> /apps/reader/books
tab.accept=.txt,.md        # file types the upload box offers
tab.page=web.html          # optional: show the app's own web page as the tab instead
tab.editor=markdown        # optional: make the tab a browser editor ("markdown" or "text")
```

### Browser editor tabs (`tab.editor=`)

With `tab.editor=text` or `tab.editor=markdown`, the tab becomes an editor for the files in `tab.dir`: a file list, a name field (edit it and save to rename), Save (Ctrl+S), download, delete, and an upload box. `markdown` adds a live preview using the same rules as the device, a formatting toolbar, Ctrl+B / Ctrl+I / Ctrl+K, clickable checkboxes, and list continuation on Enter. Typographic characters (curly quotes, dashes, ellipses) are converted to ASCII on save, because the device fonts are ASCII only.

### Custom web pages (`tab.page=`)

Instead of the standard file list, an app can ship its own page (HTML + any scripts) in its folder. The Uploader shows it inside the tab, served from `/app/<id>/<file>`, so relative links like `<script src="jszip.min.js">` just work. The page can use the same endpoints the Uploader does:

| Endpoint | |
|---|---|
| `GET /api/list?dir=/apps/<id>/...` | JSON list: `[{name, dir, size}]` |
| `POST /api/upload?path=/apps/<id>/...` | multipart upload (`file` field) |
| `GET /api/download?path=...` | file contents |
| `POST /api/delete?path=...` | delete a file or folder |
| `POST /api/rename` with form fields `from`, `to` | rename or move |

Paths must be inside `/apps` or `/notes`.

## Icons

Put an `icon.bmp` in the app folder (or name another file with `image=`).

- **Size:** **32×32** matches the built-in icons (Mac OS 5/6-style pixel art). It's pixel-doubled to 64×64 on the 3.7" and drawn 1:1 on the 2.9", so every pixel stays crisp. Other sizes work too: small icons are enlarged by whole numbers, and big ones are scaled down to fit.
- **Format:** any uncompressed BMP: 1-bit, 4-bit, 8-bit, 24-bit (MS Paint's default) or 32-bit. PNG and JPG won't work, so save as BMP.
- **Colours:** black and white only. Dark pixels become ink and light pixels paper, and 32-bit transparency counts as paper.
- **Classic style tips:** 1-pixel black outlines, a checkerboard (50% dither) for gray areas, and a 1-pixel drop shadow on the right and bottom edges.

Apps without an icon image show their `icon=` letters in a frame instead.

## Callbacks

Define any of these as global functions in `main.lua`. All are optional.

| Function | When it runs |
|---|---|
| `init()` | Once, when the app opens |
| `key(k, ch, fn)` | A key was pressed. `k` is the key code, `ch` is the character as a string (or nil for arrows, Enter, etc.), `fn` is set for Fn combos (see below). The screen redraws afterwards unless you **return false**. |
| `draw()` | The screen needs redrawing. Draw everything below the status bar each time. |
| `back()` | Esc was pressed. **Return true** if you handled it (e.g. closed a sub-screen); return false or leave `back` undefined to exit to the home screen. |
| `tick()` | About every 100 ms while the app is open. Call `screen.redraw()` if something changed. |

Every app starts fresh when opened and is completely unloaded when you leave it. Anything you want to keep must be saved with `file.write`.

## screen

Drawing uses a 1-bit screen: everything is black ink on white paper. Coordinates are pixels, with (0, 0) at the top left. Your area starts at `screen.top` (below the status bar).

| Call | |
|---|---|
| `screen.text(x, y, str [, size])` | Text. Default size = `screen.scale` |
| `screen.center(y, str [, size])` | Horizontally centred text |
| `screen.textwidth(str [, size])` | Width in pixels |
| `screen.rect(x, y, w, h [, fill])` | Rectangle outline, or filled if `fill` is true |
| `screen.line(x1, y1, x2, y2)` | Line |
| `screen.circle(x, y, r [, fill])` | Circle |
| `screen.pixel(x, y [, on])` | Set (or clear with `false`) one pixel |
| `screen.invert(x, y, w, h)` | Flip a rectangle (black to white and back); good for highlighting a selection |
| `screen.wrap(str [, cols])` | Word-wrap text into a table of lines (default `screen.cols` wide) |
| `screen.image(name, x, y [, size])` | Draw a BMP from the app's folder with its top-left corner at x,y. `size` is the square box it's fitted into: small images are enlarged by whole numbers (a 40×40 image becomes 80×80 in an 80 box), big ones shrunk. Default: its own size. Returns the drawn width and height, or `false` if it can't be read. |
| `screen.image(name, x, y, maxW, maxH)` | Fit a BMP inside a rectangle: shrunk if it's bigger, never enlarged (for pictures in documents). Images are cached while the app runs; the oldest are dropped past 16. |
| `screen.redraw([full])` | Ask for a redraw. `true` = full e-paper refresh (use when the whole screen changes) |
| `screen.font([name])` | Switch the font used by `text`/`center`/`textwidth`; returns character width, line height and ascent (baseline distance from the top of a line). No name = the system font. |

Fonts (all FreeMono, monospaced; every style of a size has the same character width, so they can be mixed on one line):

| Size | Regular | Bold | Italic | Bold italic |
|---|---|---|---|---|
| 9 pt | `mono9` | `mono9b` | `mono9i` | `mono9bi` |
| 12 pt | `mono12` | `mono12b` | `mono12i` | `mono12bi` |
| 18 pt | `mono18` | `mono18b` | | |

Constants, which let the same app work on the 2.9" and the 3.7":

| | 2.9" | 3.7" |
|---|---|---|
| `screen.W`, `screen.H` | 296 × 128 | 416 × 240 |
| `screen.top` | 13 | 25 |
| `screen.scale` | 1 | 2 |
| `screen.cw`, `screen.ch` (character size) | 6 × 8 | 12 × 16 |
| `screen.lh` (line pitch) | 10 | 20 |
| `screen.cols` (characters per line with margins) | 48 | 33 |
| `screen.margin` | 4 | 8 |

Text size is a multiplier on a 6×8 font: size `n` = `6n × 8n` pixels.

## keys

`keys.LEFT`, `keys.RIGHT`, `keys.UP`, `keys.DOWN`, `keys.ENTER`, `keys.BKSP`, `keys.TAB`, `keys.ESC`, `keys.DEL` (Shift+Bksp, delete forward)

Esc never reaches `key()`; it goes to `back()`. Printable keys arrive as their ASCII code, with `ch` set, e.g. `key(97, "a")`.

### Fn combos

On the CardKB you tap **Fn**, then a key. Every combination has its own code (128-175), and `key()` gets its name in `fn`:

- letters and digits: `"a"` ... `"z"`, `"0"` ... `"9"`
- others: `"esc"`, `"del"`, `"tab"`, `"enter"`, `"space"`, `","`, `"."`, `"left"`, `"right"`, `"up"`, `"down"`

```lua
function key(k, ch, fn)
  if fn == "c" then sys.clipboard(selectedText()) end   -- Fn+C
end
```

Conventions used by the built-in apps: Fn+C / X / V copy, cut, paste; Fn+Z / Y undo, redo; Fn+S save; Fn+Enter on the home screen shows app info. The CardKB has no key repeat or press/release events, so "hold a key" shortcuts aren't possible.

## file

All paths are relative to the app's own folder, and `..` isn't allowed. Text files only.

The one shared folder is **`/notes`**: any app can read and write `"/notes/..."` (the Notes app and the Uploader's Notes tab use it).

| Call | Returns |
|---|---|
| `file.read(name)` | contents, or `nil, message`. Files over 256 KB are refused (`nil, "too big to open (…)"`) rather than cut short, so an app can never save back a truncated copy. **Always check for `nil`** before editing and saving a file. |
| `file.write(name, text)` | true/false. Crash-safe: the old contents are kept until the new ones are completely written, and any save interrupted by a power loss is finished or undone at the next boot. Creates folders as needed. |
| `file.exists(name)` | true/false |
| `file.size(name)` | bytes |
| `file.list([folder])` | array of `{ name=, dir=, size= }`, folders first then A-Z |
| `file.mkdir(name)` | true/false |
| `file.remove(name)` | true/false (deletes a file, or a folder and everything in it) |
| `file.rename(from, to)` | true/false |

## text

A native engine for long files like books: fast even on multi-megabyte files, and it doesn't count against the 3-second limit.

| Call | Returns |
|---|---|
| `text.paginate(name, cols, rows [, lh])` | Word-wrap a text file into pages of `rows` lines of `cols` characters. Returns the page start offsets and the chapter list (from `\x01Title\x01` markers). With `lh` (the line height in pixels), `\x02img <file> <w> <h>\x02` picture markers take `ceil(h / lh)` rows (at most a page) and move to the next page when they don't fit; without it they're skipped. |
| `text.page(name, offset, cols, rows [, lh])` | The lines of the page starting at `offset`. A picture comes back as its marker line (starting with byte 2) followed by empty lines for the rest of its rows. Pass the same `lh` as to `paginate`. |

Pick `cols`/`rows` from the font you draw with, e.g. `local cw, lh = screen.font("mono9")`. Paginating a big book takes a moment, so cache the result (the eReader stores it in `cache/`).

## sys

| Call | |
|---|---|
| `sys.exit()` | Close the app and return to the home screen |
| `sys.millis()` | Milliseconds since boot |
| `sys.title([str])` | Change the status-bar title (empty = the app's name) |
| `sys.mem()` | Bytes of memory the app is using |
| `sys.stayawake(on)` | `true` keeps the device from sleeping while your app runs (e.g. a timer counting down); `false` allows it again. Ends automatically when the app closes. Normally InkDeck sleeps after a minute without key presses (adjustable in System > Settings), keeping whatever is on screen. |
| `sys.clipboard([text])` | The shared clipboard: returns its text, and sets it first if you pass text. Shared by all apps until power-off. |
| `sys.random(n)` / `sys.random(a, b)` | A random whole number from 1..n or a..b, from the ESP32's hardware random number generator, with every result equally likely. Use this rather than `math.random` for anything that should be truly random (dice, cards). |
| `sys.time()` | Seconds since 1970 (UTC), or `nil` if the clock isn't set yet |
| `sys.date([format [, time]])` | Local time as text using `strftime` codes, default `"%Y-%m-%d %H:%M"` (e.g. `sys.date("%a %d %b")`). `nil` if the clock isn't set. |
| `print(...)` | Writes to the Serial Monitor, handy for debugging |

Standard Lua available: `string`, `table`, `math`, `utf8`, `coroutine`, plus `load`, `pcall`, `pairs` etc. There's no `io`, `os`, `dofile` or `require`: use `file` instead.

## Limits

- Each app gets up to **1 MB** of memory (config: `LUA_MEM_LIMIT_KB`).
- Any one callback that runs longer than **3 seconds** is stopped (config: `LUA_TIMEOUT_MS`), so an endless loop can't freeze the device.
- Errors show on screen with the file and line number. Esc exits.

## Minimal example

```lua
-- /apps/counter/main.lua   (app.ini: name=Counter, icon=#)
local n = 0

function init()
  n = tonumber(file.read("count.txt") or "0") or 0
end

function key(k, ch)
  if k == keys.UP then n = n + 1
  elseif k == keys.DOWN then n = n - 1
  else return false end
  file.write("count.txt", tostring(n))
end

function draw()
  screen.center(screen.top + 20 * screen.scale, tostring(n), 4 * screen.scale)
  screen.center(screen.H - screen.lh, "Up / Down")
end
```
