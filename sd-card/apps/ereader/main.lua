-- =====================================================================
--  eReader — port of the Mini eReader firmware (LILYGO T5) to InkDeck
--
--  Books are .txt files in this app's books/ folder. Upload EPUB, MOBI or
--  TXT from the "Books" tab in the Uploader; they're converted in the
--  browser. \x01Chapter title\x01 markers become the chapter list, and
--  \x02img 007.bmp W H\x02 markers are pictures, in books/<book>.img/.
--
--  Library:  Up/Down select, Enter open, Bksp delete, Esc exit
--  Reading:  Right/Down/Space next page, Left/Up previous
--            . and , = 10 pages   > and < = 50 pages
--            Enter = menu (chapters, go to page, font, invert)
--            c = chapters   g = go to page   Esc = library
-- =====================================================================

local BOOKS = "books"
local CACHE = "cache"

-- Three sizes per screen: the 3.7" has more pixels, so it gets the smoother fonts
local PROFILES
if screen.scale >= 2 then
  PROFILES = { { name = "Small", font = "mono9" }, { name = "Medium", font = "mono12" }, { name = "Large", font = "mono18" } }
else
  PROFILES = { { name = "Small", font = "system" }, { name = "Medium", font = "mono9" }, { name = "Large", font = "mono12" } }
end

local settings = { font = 2, invert = false, last = "" }
local progress = {}             -- book name -> { offset = byte offset, pct = percent }

local mode = "list"             -- list | read | menu | chapters | goto | settings | delete | indexing
local books, sel, top = {}, 1, 1
local book = nil                -- the open book
local turnsSinceSave = 0
local menuItems, menuSel, menuTop = {}, 1, 1
local chapSel, chapTop = 1, 1
local gotoText = ""
local setSel, setTop = 1, 1
local delYes = false
local pending = nil             -- book waiting to be indexed
local indexingShown = false
local message = nil

-- ---------------------------------------------------------------------
--  Saved state
-- ---------------------------------------------------------------------
local function loadSettings()
  local s = file.read("settings.txt")
  if not s then return end
  for k, v in s:gmatch("([%w_]+)=([^\n]*)") do
    if k == "font" then settings.font = math.max(1, math.min(#PROFILES, tonumber(v) or 2))
    elseif k == "invert" then settings.invert = (v == "1")
    elseif k == "last" then settings.last = v end
  end
end

local function saveSettings()
  file.write("settings.txt", "font=" .. settings.font .. "\ninvert=" .. (settings.invert and "1" or "0") ..
                             "\nlast=" .. settings.last .. "\n")
end

local function loadProgress()
  local s = file.read("progress.txt")
  if not s then return end
  for name, off, pct in s:gmatch("([^\t\n]+)\t(%d+)\t(%d+)") do
    progress[name] = { offset = tonumber(off), pct = tonumber(pct) }
  end
end

local function saveProgress()
  local out = {}
  for name, p in pairs(progress) do out[#out + 1] = name .. "\t" .. p.offset .. "\t" .. p.pct end
  table.sort(out)
  file.write("progress.txt", table.concat(out, "\n") .. "\n")
end

-- ---------------------------------------------------------------------
--  Library
-- ---------------------------------------------------------------------
local function titleOf(name)
  local t = name:gsub("%.[tT][xX][tT]$", "")
  t = t:gsub("[_%-]", " ")
  return t
end

local function scanBooks()
  books = {}
  for _, e in ipairs(file.list(BOOKS)) do
    if not e.dir and e.name:lower():match("%.txt$") then
      books[#books + 1] = { name = e.name, title = titleOf(e.name) }
    end
  end
  table.sort(books, function(a, b) return a.title:lower() < b.title:lower() end)
  books[#books + 1] = { settings = true, title = "[Settings]" }
  if sel > #books then sel = #books end
end

local function deleteBook(name)
  file.remove(BOOKS .. "/" .. name)
  local base = name:gsub("%.[tT][xX][tT]$", "")
  for _, ext in ipairs({ ".raw", ".src", ".img" }) do       -- saved text, original file, pictures
    if file.exists(BOOKS .. "/" .. base .. ext) then file.remove(BOOKS .. "/" .. base .. ext) end
  end
  for _, e in ipairs(file.list(CACHE)) do
    if e.name:sub(1, #name + 1) == name .. "." then file.remove(CACHE .. "/" .. e.name) end
  end
  progress[name] = nil
  saveProgress()
  if settings.last == name then settings.last = ""; saveSettings() end
end

-- ---------------------------------------------------------------------
--  Page index cache: cache/<book>.<cols>x<rows>x<line height>.idx
--    line 1: book size in bytes (a changed book rebuilds the index)
--    line 2: page start offsets, comma separated
--    then:   <page>\t<chapter title> per chapter
-- ---------------------------------------------------------------------
local function cacheName(name, cols, rows, lh)
  return CACHE .. "/" .. name .. "." .. cols .. "x" .. rows .. "x" .. lh .. ".idx"
end

local function loadCache(name, cols, rows, lh, size)
  local s = file.read(cacheName(name, cols, rows, lh))
  if not s then return nil end
  local header, offs, rest = s:match("^([^\n]*)\n([^\n]*)\n?(.*)$")
  if not header or tonumber(header) ~= size then return nil end
  local pages = {}
  for n in offs:gmatch("%d+") do pages[#pages + 1] = tonumber(n) end
  if #pages == 0 then return nil end
  local chapters = {}
  for pg, title in rest:gmatch("(%d+)\t([^\n]*)") do
    chapters[#chapters + 1] = { page = tonumber(pg), title = title }
  end
  return pages, chapters
end

local function saveCache(name, cols, rows, lh, size, pages, chapters)
  local out = { tostring(size), table.concat(pages, ",") }
  for _, c in ipairs(chapters) do
    out[#out + 1] = c.page .. "\t" .. c.title:gsub("[\t\n]", " ")
  end
  file.write(cacheName(name, cols, rows, lh), table.concat(out, "\n") .. "\n")
end

-- Last page that starts at or before a byte offset
local function pageForOffset(pages, off)
  local lo, hi = 1, #pages
  while lo < hi do
    local mid = (lo + hi + 1) // 2
    if pages[mid] <= off then lo = mid else hi = mid - 1 end
  end
  return lo
end

-- ---------------------------------------------------------------------
--  Reading
-- ---------------------------------------------------------------------
local function layout()
  local cw, lh = screen.font(PROFILES[settings.font].font)
  screen.font()
  local s = screen.scale
  local y0 = screen.top + 2 * s
  local cols = math.floor((screen.W - 2 * screen.margin - 3 * s) / cw)   -- room for the scrollbar
  local rows = math.floor((screen.H - y0 - s) / lh)
  return cols, rows, lh, y0
end

-- PDF books (converted as page images): the PDF page a screen shows, read from
-- the first picture at or after the screen's start ("\2img p0012_1.bmp ...")
local function pdfPageAt(i)
  if not (book and book.viewText and book.pages[i]) then return nil end
  return tonumber(book.viewText:match("\2img p(%d+)_", book.pages[i] + 1))
end

local function updateTitle()
  if not book then return end
  if book.view then
    sys.title((pdfPageAt(book.page) or "?") .. "/" .. book.pdfPages .. " " .. book.title)
  else
    sys.title(book.page .. "/" .. #book.pages .. " " .. book.title)
  end
end

local function loadLines()
  book.lines = text.page(BOOKS .. "/" .. book.name, book.pages[book.page], book.cols, book.rows, book.lh) or {}
end

local function savePosition()
  if not book then return end
  local total = #book.pages
  progress[book.name] = {
    offset = book.pages[book.page],
    pct = math.floor(100 * (book.page - 1) / math.max(1, total - 1)),
  }
  saveProgress()
  turnsSinceSave = 0
end

local function gotoPage(n)
  n = math.max(1, math.min(#book.pages, n))
  if n == book.page then return false end
  book.page = n
  loadLines()
  updateTitle()
  turnsSinceSave = turnsSinceSave + 1
  if turnsSinceSave >= 5 then savePosition() end
  return true
end

local function finishOpen(p, pages, chapters)
  book = { name = p.name, title = titleOf(p.name), pages = pages, chapters = chapters,
           cols = p.cols, rows = p.rows, lh = p.lh, y0 = p.y0,
           view = p.view, pdfPages = p.pdfPages }
  if p.view then book.viewText = file.read(BOOKS .. "/" .. p.name) end   -- to map screens to PDF pages
  local saved = progress[p.name]
  book.page = saved and pageForOffset(pages, saved.offset) or 1
  loadLines()
  turnsSinceSave = 0
  settings.last = p.name
  saveSettings()
  mode = "read"
  updateTitle()
  screen.redraw(true)
end

local function openBook(name)
  local cols, rows, lh, y0 = layout()
  local p = { name = name, cols = cols, rows = rows, lh = lh, y0 = y0, size = file.size(BOOKS .. "/" .. name) }
  -- A PDF converted as page images: lay it out in pixels (1-pixel "lines"), so
  -- each strip fills the screen exactly, whatever the font setting
  local view = file.read(BOOKS .. "/" .. name:gsub("%.[tT][xX][tT]$", "") .. ".view")
  if view then
    p.view = true
    p.pdfPages = tonumber(view:match("pdf (%d+)")) or 0
    p.lh = 1
    p.rows = math.floor(screen.H - y0 - screen.scale)
    p.cols = 1000
  end
  cols, rows, lh = p.cols, p.rows, p.lh
  local pages, chapters = loadCache(name, cols, rows, lh, p.size)
  if pages then finishOpen(p, pages, chapters); return end
  -- First open (or new font size): show a message, then index in tick()
  pending = p
  indexingShown = false
  mode = "indexing"
  sys.title("eReader")
  screen.redraw(true)
end

local function closeBook()
  savePosition()
  settings.last = ""
  saveSettings()
  book = nil
  mode = "list"
  sys.title("")
  scanBooks()
  screen.redraw(true)
end

local function currentChapter()
  local idx = 1
  for i, c in ipairs(book.chapters) do if c.page <= book.page then idx = i end end
  return idx
end

local function buildMenu()
  menuItems = {}
  if #book.chapters > 0 then menuItems[#menuItems + 1] = { id = "chapters", label = "Chapters" } end
  menuItems[#menuItems + 1] = { id = "goto",   label = "Go to page" }
  menuItems[#menuItems + 1] = { id = "font",   label = "Font: " .. PROFILES[settings.font].name }
  menuItems[#menuItems + 1] = { id = "invert", label = "Invert: " .. (settings.invert and "On" or "Off") }
  menuItems[#menuItems + 1] = { id = "close",  label = "Back to library" }
  if menuSel > #menuItems then menuSel = 1 end
end

local function openChapters()
  chapSel = currentChapter()
  chapTop = 1
  mode = "chapters"
end

-- Change font size; an open book is re-paginated and keeps its place
local function cycleFont()
  settings.font = settings.font % #PROFILES + 1
  saveSettings()
  if book then
    savePosition()
    local name = book.name
    book = nil
    openBook(name)
  end
end

-- ---------------------------------------------------------------------
--  Drawing helpers
-- ---------------------------------------------------------------------
local function fit(s, n)
  if #s <= n then return s end
  return s:sub(1, n - 1) .. "~"
end

-- List with an inverted selection bar; returns the adjusted scroll position
local function drawList(items, selected, first, y0, rows)
  if selected < first then first = selected end
  if selected >= first + rows then first = selected - rows + 1 end
  for r = 0, rows - 1 do
    local item = items[first + r]
    if not item then break end
    local y = y0 + r * screen.lh
    screen.text(screen.margin, y + screen.scale, item)
    if first + r == selected then screen.invert(0, y, screen.W, screen.lh) end
  end
  if first > 1 then screen.text(screen.W - screen.cw - screen.margin, y0, "^") end
  if first + rows <= #items then screen.text(screen.W - screen.cw - screen.margin, y0 + (rows - 1) * screen.lh, "v") end
  return first
end

local function header(title)
  screen.text(screen.margin, screen.top + 2 * screen.scale, title)
  screen.rect(0, screen.top + screen.lh + screen.scale, screen.W, screen.scale, true)
  return screen.top + screen.lh + 3 * screen.scale
end

local function footer(hint)
  screen.text(screen.margin, screen.H - screen.lh + screen.scale, fit(hint, screen.cols))
end

local function listRowsFrom(y0)
  return math.floor((screen.H - screen.lh - y0) / screen.lh)
end

-- ---------------------------------------------------------------------
--  Callbacks
-- ---------------------------------------------------------------------
function init()
  loadSettings()
  loadProgress()
  scanBooks()
  -- Reopen the book that was being read if the device lost power mid-book
  if settings.last ~= "" and file.exists(BOOKS .. "/" .. settings.last) then
    for i, b in ipairs(books) do if b.name == settings.last then sel = i end end
    openBook(settings.last)
  end
end

function tick()
  if mode == "indexing" and indexingShown then
    local p = pending
    pending = nil
    local pages, chapters = text.paginate(BOOKS .. "/" .. p.name, p.cols, p.rows, p.lh)
    if not pages then
      message = "Couldn't open " .. p.name
      mode = "list"
      sys.title("")
      screen.redraw(true)
      return
    end
    saveCache(p.name, p.cols, p.rows, p.lh, p.size, pages, chapters)
    finishOpen(p, pages, chapters)
  end
end

function key(k, ch)
  if mode == "indexing" then return false end

  if mode == "list" then
    message = nil
    if k == keys.UP then sel = sel > 1 and sel - 1 or #books
    elseif k == keys.DOWN then sel = sel < #books and sel + 1 or 1
    elseif k == keys.ENTER then
      if books[sel].settings then mode = "settings"; setSel = 1
      else openBook(books[sel].name) end
    elseif k == keys.BKSP and not books[sel].settings then
      delYes = false
      mode = "delete"
    else return false end
    return
  end

  if mode == "read" then
    if k == keys.RIGHT or k == keys.DOWN or ch == " " then return gotoPage(book.page + 1)
    elseif k == keys.LEFT or k == keys.UP then return gotoPage(book.page - 1)
    elseif ch == "." then return gotoPage(book.page + 10)
    elseif ch == "," then return gotoPage(book.page - 10)
    elseif ch == ">" then return gotoPage(book.page + 50)
    elseif ch == "<" then return gotoPage(book.page - 50)
    elseif k == keys.ENTER then buildMenu(); mode = "menu"
    elseif ch == "c" and #book.chapters > 0 then openChapters()
    elseif ch == "g" then gotoText = ""; mode = "goto"
    else return false end
    return
  end

  if mode == "menu" then
    if k == keys.UP then menuSel = menuSel > 1 and menuSel - 1 or #menuItems
    elseif k == keys.DOWN then menuSel = menuSel < #menuItems and menuSel + 1 or 1
    elseif k == keys.ENTER then
      local id = menuItems[menuSel].id
      if id == "chapters" then openChapters()
      elseif id == "goto" then gotoText = ""; mode = "goto"
      elseif id == "font" then cycleFont(); if book then buildMenu() end
      elseif id == "invert" then settings.invert = not settings.invert; saveSettings(); buildMenu(); screen.redraw(true)
      elseif id == "close" then closeBook() end
    else return false end
    return
  end

  if mode == "chapters" then
    local n = #book.chapters
    if k == keys.UP then chapSel = chapSel > 1 and chapSel - 1 or n
    elseif k == keys.DOWN then chapSel = chapSel < n and chapSel + 1 or 1
    elseif k == keys.ENTER then
      gotoPage(book.chapters[chapSel].page)
      savePosition()
      mode = "read"
      updateTitle()
    else return false end
    return
  end

  if mode == "goto" then
    if ch and (ch:match("%d") or (ch == "%" and #gotoText > 0 and not gotoText:find("%%"))) then
      if #gotoText < 6 then gotoText = gotoText .. ch end
    elseif k == keys.BKSP then gotoText = gotoText:sub(1, -2)
    elseif k == keys.ENTER then
      local n = tonumber(gotoText:match("^(%d+)"))
      if n then
        if gotoText:find("%%") then n = 1 + math.floor((#book.pages - 1) * math.min(n, 100) / 100)
        elseif book.view then                               -- a PDF page number: first screen showing it
          local lo, hi = 1, #book.pages
          while lo < hi do
            local mid = (lo + hi) // 2
            if (pdfPageAt(mid) or 0) < n then lo = mid + 1 else hi = mid end
          end
          n = lo
        end
        gotoPage(n)
        savePosition()
      end
      mode = "read"
      updateTitle()
    else return false end
    return
  end

  if mode == "settings" then
    if k == keys.UP or k == keys.DOWN then setSel = setSel == 1 and 2 or 1
    elseif k == keys.ENTER then
      if setSel == 1 then cycleFont()
      else settings.invert = not settings.invert; saveSettings(); screen.redraw(true) end
    else return false end
    return
  end

  if mode == "delete" then
    if k == keys.LEFT or k == keys.RIGHT or k == keys.UP or k == keys.DOWN then delYes = not delYes
    elseif ch == "y" then delYes = true
    elseif ch == "n" then delYes = false
    elseif k == keys.ENTER then
      if delYes then
        message = "Deleted " .. books[sel].title
        deleteBook(books[sel].name)
        scanBooks()
      end
      mode = "list"
    else return false end
    return
  end
end

function back()
  if mode == "list" then return false end                 -- leave the app
  if mode == "read" then closeBook()
  elseif mode == "menu" or mode == "chapters" or mode == "goto" then mode = "read"; updateTitle()
  elseif mode == "settings" or mode == "delete" then mode = "list"
  end                                                      -- indexing: wait for it
  return true
end

-- ---------------------------------------------------------------------
--  Screens
-- ---------------------------------------------------------------------
local function drawLibrary()
  local y0 = header("MY BOOKS")
  if #books <= 1 then
    screen.text(screen.margin, y0 + screen.lh, "No books yet.")
    screen.text(screen.margin, y0 + 2 * screen.lh, "Upload them from the Books tab")
    screen.text(screen.margin, y0 + 3 * screen.lh, "in the Uploader app.")
    y0 = y0 + 5 * screen.lh
  end
  local items = {}
  for _, b in ipairs(books) do
    local suffix = ""
    if not b.settings and progress[b.name] then suffix = " " .. progress[b.name].pct .. "%" end
    items[#items + 1] = fit(b.title, screen.cols - #suffix - 2) .. suffix
  end
  local rows = listRowsFrom(y0)
  if message then rows = rows - 1 end
  top = drawList(items, sel, top, y0, rows)
  if message then screen.text(screen.margin, screen.H - 2 * screen.lh + screen.scale, fit(message, screen.cols)) end
  footer("Enter=read  Bksp=delete  Esc=exit")
end

local imageRects = {}           -- where this page's pictures are (kept un-inverted)

-- A picture line: "\2img 007.bmp W H". It takes ceil(H / line height) rows
-- (at most a page), exactly as the text engine counted them.
local function drawPicture(line, y)
  local name, w, h = line:match("^\2img (%S+) (%d+) (%d+)")
  if not name then return end
  w, h = tonumber(w), tonumber(h)
  local s = screen.scale
  local areaW = screen.W - 2 * screen.margin - 3 * s
  local rows = math.min(book.rows, math.max(1, (h + book.lh - 1) // book.lh))
  local areaH = rows * book.lh
  local k = math.min(1, areaW / w, areaH / h)              -- never enlarged
  local dw, dh = math.floor(w * k), math.floor(h * k)
  local x = screen.margin + (areaW - dw) // 2
  local yy = y + (areaH - dh) // 2
  local folder = BOOKS .. "/" .. book.name:gsub("%.[tT][xX][tT]$", "") .. ".img/"
  local okW, okH = screen.image(folder .. name, x, yy, areaW, areaH)
  if okW then
    imageRects[#imageRects + 1] = { x, yy, okW, okH }
  else                                                     -- picture file missing: say so
    screen.rect(x, yy, dw, dh)
    screen.text(x + 2 * s, yy + 2 * s, "[picture]")
  end
end

local function drawPage()
  imageRects = {}
  screen.font(PROFILES[settings.font].font)
  for i, line in ipairs(book.lines) do
    local y = book.y0 + (i - 1) * book.lh
    if line:byte(1) == 2 then drawPicture(line, y)
    else screen.text(screen.margin, y, line) end
  end
  screen.font()

  -- Scrollbar on the right edge, as on the original
  local s = screen.scale
  local x = screen.W - s
  local t, b = screen.top + 2 * s, screen.H - 2 * s
  for y = t, b, 3 * s do screen.rect(x, y, s, s, true) end
  local ih = 6 * s
  local iy = t + math.floor((b - t - ih) * (book.page - 1) / math.max(1, #book.pages - 1))
  screen.rect(x, iy, s, ih, true)
end

local function drawMenu()
  local y0 = header("Page " .. book.page .. " / " .. #book.pages)
  local items = {}
  for _, m in ipairs(menuItems) do items[#items + 1] = m.label end
  menuTop = drawList(items, menuSel, menuTop, y0, listRowsFrom(y0))
  footer("Enter=select  Esc=back to book")
end

local function drawChapters()
  local y0 = header("Chapters")
  local items = {}
  for _, c in ipairs(book.chapters) do
    local pg = " p" .. (book.view and (pdfPageAt(c.page) or c.page) or c.page)
    items[#items + 1] = fit(c.title, screen.cols - #pg - 2) .. string.rep(" ", math.max(1, screen.cols - 2 - #fit(c.title, screen.cols - #pg - 2) - #pg)) .. pg
  end
  chapTop = drawList(items, chapSel, chapTop, y0, listRowsFrom(y0))
  footer("Enter=go  Esc=back")
end

local function drawGoto()
  local s = screen.scale
  local y0 = header("Go to page...")
  screen.text(screen.margin, y0 + screen.lh, "Page 1 - " .. #book.pages .. "  (now " .. book.page .. ")")
  local boxY = y0 + 2 * screen.lh + 2 * s
  local boxH = screen.lh + 4 * s
  screen.rect(screen.margin, boxY, screen.W - 2 * screen.margin, boxH)
  screen.text(2 * screen.margin, boxY + 3 * s, gotoText)
  screen.rect(2 * screen.margin + #gotoText * screen.cw, boxY + 3 * s, 5 * s, screen.ch, true)
  screen.text(screen.margin, boxY + boxH + screen.lh, "Type a page, or a percent: 50%")
  footer("Enter=go  Esc=back")
end

local function drawSettings()
  local y0 = header("SETTINGS")
  local items = { "Font: " .. PROFILES[settings.font].name, "Invert: " .. (settings.invert and "On" or "Off") }
  setTop = drawList(items, setSel, setTop, y0, listRowsFrom(y0))
  footer("Enter=change  Esc=back")
end

local function drawDelete()
  local s = screen.scale
  local y0 = header("Delete book?")
  screen.text(screen.margin, y0 + screen.lh, fit(books[sel].title, screen.cols))
  local y = y0 + 3 * screen.lh
  local w = 6 * screen.cw
  local yesX, noX = screen.W // 4 - w // 2, screen.W * 3 // 4 - w // 2
  screen.text(yesX + screen.cw * 1.5, y + 2 * s, "YES")
  screen.text(noX + screen.cw * 2, y + 2 * s, "NO")
  screen.rect(yesX, y, w, screen.lh + 2 * s)
  screen.rect(noX, y, w, screen.lh + 2 * s)
  if delYes then screen.invert(yesX, y, w, screen.lh + 2 * s) else screen.invert(noX, y, w, screen.lh + 2 * s) end
  footer("Left/Right=choose  Enter=confirm")
end

local function drawIndexing()
  local mid = screen.top + (screen.H - screen.top) // 2
  screen.center(mid - screen.lh, "Building index...")
  screen.center(mid + screen.scale, "(first open only)")
  if pending then screen.center(mid + 2 * screen.lh, fit(titleOf(pending.name), screen.cols)) end
  indexingShown = true
end

function draw()
  if     mode == "list"     then drawLibrary()
  elseif mode == "read"     then drawPage()
  elseif mode == "menu"     then drawMenu()
  elseif mode == "chapters" then drawChapters()
  elseif mode == "goto"     then drawGoto()
  elseif mode == "settings" then drawSettings()
  elseif mode == "delete"   then drawDelete()
  elseif mode == "indexing" then drawIndexing()
  end
  if settings.invert then
    screen.invert(0, screen.top, screen.W, screen.H - screen.top)
    if mode == "read" then                                 -- pictures stay the right way round
      for _, r in ipairs(imageRects) do screen.invert(r[1], r[2], r[3], r[4]) end
    end
  end
end
