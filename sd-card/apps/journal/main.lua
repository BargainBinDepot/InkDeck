-- =====================================================================
--  Journal — a month calendar with a markdown entry for each day
--  (generated from apps/markdown/main.lua; the calendar is added at the end)
--
--  Calendar: arrows move by day / week, Fn+Left/Right = month,
--            Fn+Up/Down = year, t = today, Enter = open the day,
--            Bksp twice = delete an entry, Esc = exit
--  Editor:   the Markdown app's editor. New days start with the date as a
--            heading. Esc saves automatically and goes back to the calendar;
--            an entry left with only its heading isn't saved.
--  Entries:  entries/YYYY-MM-DD.md (also in the Uploader's Journal tab)
-- =====================================================================

-- ---- App settings. The Notes app is this same file with PLAIN = true ----
local PLAIN      = false               -- true = plain text: no markdown formatting
local DOCS       = "entries"           -- one file per day: entries/YYYY-MM-DD.md
local EXT        = ".md"
local LIST_TITLE = "JOURNAL"
local NEW_LABEL  = "+ New entry"
local NEW_TITLE  = "New entry"
local HELP_TITLE = "JOURNAL HELP"
-- --------------------------------------------------------------------------

local S = screen.scale
local M = screen.margin

-- Fonts: body text is FreeMono 9pt on both screens; headings get bigger
local FONT = {
  body = "mono9", b = "mono9b", i = "mono9i", bi = "mono9bi",
  h1 = S >= 2 and "mono18b" or "mono12b",
  h2 = S >= 2 and "mono12b" or "mono9b",
  h3 = "mono9b",
}
local FM = {}                     -- font metrics: cw, lh, asc
for _, name in pairs(FONT) do
  local cw, lh, asc = screen.font(name)
  FM[name] = { cw = cw, lh = lh, asc = asc }
end
screen.font()
local BODY = FM[FONT.body]

-- ---------------------------------------------------------------------
--  State
-- ---------------------------------------------------------------------
local mode = "list"               -- list | edit | help | save | name | delete
local files, fsel, ftop = {}, 1, 1
local message = nil

local doc = { "" }                -- one string per paragraph (a line of the file)
local fileName = nil              -- current file (nil = new document)
local dirty = false
local cr, cc = 1, 0               -- cursor: paragraph, column (0 = before first char)
local wantX = nil                 -- remembered x for up/down
local topPara, topLine = 1, 1     -- scroll position: first visible paragraph / its display line
local inCode = {}                 -- paragraph index -> inside a ``` block

local helpPage = 1
local saveSel = 1
local afterName = "close"         -- what happens after naming: "close" or "stay" (Fn+S)
local renameFrom = nil            -- file being renamed (rename screen)
local renameBack = "list"         -- where rename returns to: "list" or "edit"
local anchor = nil                -- selection start {r=, c=}; the cursor is the other end
local undo, redo = {}, {}
local lastKind = nil              -- groups typing into one undo step per word
local flash = nil                 -- short message shown in the title until the next key
local nameText, nameWarn = "", nil
local delYes = false

-- ---------------------------------------------------------------------
--  Files
-- ---------------------------------------------------------------------
local function scanFiles()
  files = {}
  for _, e in ipairs(file.list(DOCS)) do
    local n = e.name:lower()
    if not e.dir and (n:match("%.txt$") or (not PLAIN and n:match("%.md$"))) then
      files[#files + 1] = e
    end
  end
  table.sort(files, function(a, b) return a.name:lower() < b.name:lower() end)
  if fsel > #files + 1 then fsel = #files + 1 end
end

local function updateTitle()
  if flash then sys.title(flash); return end
  if anchor and mode == "edit" then sys.title("Select: Fn+C/X"); return end
  sys.title((fileName or NEW_TITLE) .. (dirty and " *" or ""))
end

local function computeFences()
  inCode = {}
  if PLAIN then return end
  local on = false
  for i, p in ipairs(doc) do
    if p:match("^%s*```") then inCode[i] = "fence"; on = not on
    elseif on then inCode[i] = "code" end
  end
end

local function openDoc(name)
  -- Read first: if the file can't be read in full (too big, card error), don't
  -- open it at all. Opening it empty and saving would wipe the real file.
  local text = ""
  if name then
    local t, err = file.read(DOCS .. "/" .. name)
    if not t then
      message = (err or "can't read the file"):gsub("^%l", string.upper)   -- fits one line
      return false
    end
    text = t
  end
  doc = {}
  fileName = name
  if name then
    text = text:gsub("\r", "")
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do doc[#doc + 1] = line end
    if #doc > 1 and doc[#doc] == "" then doc[#doc] = nil end   -- the file's final newline
  end
  if #doc == 0 then doc = { "" } end
  cr, cc, wantX = 1, 0, nil
  topPara, topLine = 1, 1
  dirty = false
  anchor, undo, redo, lastKind, flash = nil, {}, {}, nil, nil
  computeFences()
  mode = "edit"
  updateTitle()
  screen.redraw(true)
end

local function suggestName()
  local src = nil
  for _, p in ipairs(doc) do
    if p:match("%S") then src = p; break end
  end
  src = (src or "untitled"):gsub("^%s*#+%s*", "")
  src = src:gsub("^%s*[-*+]%s+%[[ xX]%]%s*", ""):gsub("^%s*[-*+>]%s+", ""):gsub("^%s*%d+[.)]%s+", "")
  src = src:gsub("[%*_~`%[%]%(%)>#]", " ")
  local words = {}
  for w in src:gmatch("[%w%-']+") do
    if w:match("%w") then words[#words + 1] = w end
    if #words == 4 then break end
  end
  local base = #words > 0 and table.concat(words, "_") or "untitled"
  local name, n = base .. EXT, 2
  while file.exists(DOCS .. "/" .. name) do name = base .. "_" .. n .. EXT; n = n + 1 end
  return name
end

local function saveDoc(name)
  local ok = file.write(DOCS .. "/" .. name, table.concat(doc, "\n") .. "\n")
  if ok then
    fileName = name
    dirty = false
    message = "Saved " .. name
  else
    message = "Couldn't save " .. name
  end
  return ok
end

local function closeDoc()
  mode = "list"
  sys.title("")
  scanFiles()
  for i, f in ipairs(files) do if f.name == fileName then fsel = i + 1 end end
  screen.redraw(true)
end

-- ---------------------------------------------------------------------
--  Markdown: blocks
-- ---------------------------------------------------------------------
-- What kind of paragraph this is, and how long its markdown prefix is
local function classify(i)
  local p = doc[i]
  if PLAIN then return { kind = "p", plen = 0 } end
  if inCode[i] == "fence" then return { kind = "fence", plen = 0 } end
  if inCode[i] == "code" then return { kind = "code", plen = 0 } end

  local hashes, rest = p:match("^(#+)%s(.*)$")
  if hashes then return { kind = "h", level = math.min(#hashes, 3), plen = #hashes + 1 } end
  if p:match("^%s*%-%-%-+%s*$") or p:match("^%s*%*%*%*+%s*$") or p:match("^%s*___+%s*$") then
    return { kind = "hr", plen = #p }
  end
  local ind, box = p:match("^(%s*)[-*+] %[([ xX])%] ")
  if ind then
    return { kind = "task", indent = #ind, checked = box ~= " ", plen = #ind + 6 }
  end
  ind = p:match("^(%s*)[-*+] ")
  if ind then return { kind = "ul", indent = #ind, plen = #ind + 2 } end
  local num, sep
  ind, num, sep = p:match("^(%s*)(%d+)([.)]) ")
  if ind then return { kind = "ol", indent = #ind, num = num, plen = #ind + #num + 2 } end
  if p:match("^>") then return { kind = "quote", plen = p:match("^> ") and 2 or 1 } end
  return { kind = "p", plen = 0 }
end

-- ---------------------------------------------------------------------
--  Markdown: inline styles
--  Returns one cell per character: { ch, b, i, s, code, link, marker }
-- ---------------------------------------------------------------------
local TOKENS = {
  { "***", function(c) c.b = true; c.i = true end },
  { "**",  function(c) c.b = true end },
  { "__",  function(c) c.b = true end },
  { "~~",  function(c) c.s = true end },
  { "*",   function(c) c.i = true end },
  { "_",   function(c) c.i = true end },
}

local function parseInline(s)
  local n = #s
  local cells, used = {}, {}
  for k = 1, n do cells[k] = { ch = s:sub(k, k) } end
  if PLAIN then return cells end

  -- `code` first: nothing inside it is parsed
  local k = 1
  while k <= n do
    if s:sub(k, k) == "`" then
      local close = s:find("`", k + 1, true)
      if close and close > k + 1 then
        cells[k].marker, cells[close].marker = true, true
        for j = k, close do used[j] = true end
        for j = k + 1, close - 1 do cells[j].code = true end
        k = close + 1
      else
        k = k + 1
      end
    else
      k = k + 1
    end
  end

  -- [text](url): text is underlined, the rest hidden
  local from = 1
  while true do
    local a, b, text = s:find("%[([^%]]+)%]%([^%)%s]*%)", from)
    if not a then break end
    local free = true
    for j = a, b do if used[j] then free = false; break end end
    if free then
      cells[a].marker = true
      for j = a + 1, a + #text do cells[j].link = true end
      for j = a + #text + 1, b do cells[j].marker = true; used[j] = true end
      used[a] = true
    end
    from = b + 1
  end

  local function free(pos, len)
    for j = pos, pos + len - 1 do if used[j] then return false end end
    return true
  end

  -- Emphasis, longest markers first. A span only counts once it's closed.
  for _, t in ipairs(TOKENS) do
    local tok, apply = t[1], t[2]
    local L = #tok
    local underscore = tok:sub(1, 1) == "_"
    k = 1
    while k <= n - L + 1 do
      local nextc = s:sub(k + L, k + L)
      local prevc = k > 1 and s:sub(k - 1, k - 1) or " "
      if s:sub(k, k + L - 1) == tok and free(k, L) and nextc ~= "" and nextc ~= " "
         and (not underscore or prevc:match("[%s%p]")) then
        local found = nil
        local j = k + L + 1                                 -- at least one character inside
        while j <= n - L + 1 do
          if s:sub(j, j + L - 1) == tok and free(j, L) and s:sub(j - 1, j - 1) ~= " " then
            local after = s:sub(j + L, j + L)
            if not underscore or after == "" or after:match("[%s%p]") then found = j; break end
          end
          j = j + 1
        end
        if found then
          for q = k, k + L - 1 do cells[q].marker = true; used[q] = true end
          for q = found, found + L - 1 do cells[q].marker = true; used[q] = true end
          for q = k + L, found - 1 do if not cells[q].marker then apply(cells[q]) end end
          k = found + L
        else
          k = k + 1
        end
      else
        k = k + 1
      end
    end
  end
  return cells
end

-- ---------------------------------------------------------------------
--  Layout: a paragraph becomes display lines
--  line = { cells = {...}, font =, h =, x = left edge, deco = first-line decoration }
--  cell.ri = the character's index in the paragraph (active paragraph only)
-- ---------------------------------------------------------------------
local function fontFor(cell, headFont)
  if headFont then return headFont end
  if cell.code then return FONT.body end
  if cell.b and cell.i then return FONT.bi end
  if cell.b then return FONT.b end
  if cell.i then return FONT.i end
  return FONT.body
end

local layoutCache = {}

local function layout(i)
  local active = (i == cr) and mode == "edit"
  local key = doc[i] .. (active and "\1" or "\0") .. tostring(inCode[i])
  local c = layoutCache[i]
  if c and c.key == key then return c.lines end

  local p = doc[i]
  local blk = classify(i)
  local headFont = blk.kind == "h" and FONT["h" .. blk.level] or nil
  local fm = FM[headFont or FONT.body]
  local cells = {}
  local indentPx, deco = 0, nil

  if blk.kind == "code" or blk.kind == "fence" then
    for k = 1, #p do cells[#cells + 1] = { ch = p:sub(k, k), ri = k, plain = true } end
    indentPx = 3 * S
    deco = { kind = blk.kind }
  elseif blk.kind == "hr" and not active then
    deco = { kind = "hr" }
  else
    local prefix = p:sub(1, blk.plen)
    local content = p:sub(blk.plen + 1)
    if active then
      for k = 1, #prefix do cells[#cells + 1] = { ch = prefix:sub(k, k), ri = k } end
    end
    local inl = parseInline(content)
    for k, cell in ipairs(inl) do
      if active or not cell.marker then
        cell.ri = blk.plen + k
        if cell.marker then cell.b, cell.i, cell.s, cell.link, cell.code = nil, nil, nil, nil, nil end
        cells[#cells + 1] = cell
      end
    end
    if active then
      -- raw text: continuation lines line up under the text, after the prefix
      if blk.kind ~= "p" and blk.kind ~= "h" then indentPx = blk.plen * BODY.cw end
    else
      local lead = (blk.indent or 0) * BODY.cw // 2
      if blk.kind == "ul" or blk.kind == "task" then indentPx = lead + 2 * BODY.cw
      elseif blk.kind == "ol" then indentPx = lead + (#blk.num + 2) * BODY.cw
      elseif blk.kind == "quote" then indentPx = 2 * BODY.cw end
      deco = { kind = blk.kind, lead = lead, num = blk.num, checked = blk.checked }
    end
    deco = deco or {}
    deco.activeKind = blk.kind
  end

  -- In the paragraph being edited, the raw prefix ("- ", "> "...) is part of
  -- the first line, so only the wrapped lines are indented
  local firstIndent = indentPx
  if active and blk.kind ~= "code" and blk.kind ~= "fence" then firstIndent = 0 end

  -- Word wrap
  local function colsFor(n) return math.max(4, (screen.W - 2 * M - (n == 1 and firstIndent or indentPx)) // fm.cw) end
  local lines = {}
  local line = {}
  local lastSpace = nil
  for _, cell in ipairs(cells) do
    line[#line + 1] = cell
    if cell.ch == " " then lastSpace = #line end
    local cols = colsFor(#lines + 1)
    if #line > cols then
      local cut = lastSpace or cols
      local rest = {}
      for q = cut + 1, #line do rest[#rest + 1] = line[q] end
      for q = #line, cut + 1, -1 do line[q] = nil end
      lines[#lines + 1] = line
      line = rest
      lastSpace = nil
      for q, cc2 in ipairs(line) do if cc2.ch == " " then lastSpace = q end end
    end
  end
  lines[#lines + 1] = line

  local out = {}
  for n, l in ipairs(lines) do
    local h = fm.lh
    if blk.kind == "h" and n == #lines and not active then h = h + (blk.level <= 2 and 3 * S or S) end
    if #p == 0 and not active then h = fm.lh // 2 end            -- blank lines take half a line
    if blk.kind == "hr" and not active then h = BODY.lh // 2 + 2 * S end
    out[n] = { cells = l, font = headFont, fm = fm, h = h, x = M + (n == 1 and firstIndent or indentPx),
               deco = (n == 1) and deco or (deco and { kind = deco.kind, cont = true }),
               underline = blk.kind == "h" and blk.level <= 2 and not active and n == #lines }
  end
  layoutCache[i] = { key = key, lines = out }
  return out
end

local function invalidate() layoutCache = {} end

-- ---------------------------------------------------------------------
--  Drawing a display line (ox shifts everything right, used by the help page)
-- ---------------------------------------------------------------------
local function drawLine(l, y, ox)
  ox = ox or 0
  local left = M + ox
  local d = l.deco

  -- Block decorations
  if d then
    local kind = d.kind
    if kind == "hr" then
      screen.rect(left, y + l.h // 2 - S // 2, screen.W - M - left, S, true)
    elseif kind == "quote" or kind == "code" or kind == "fence" then
      screen.rect(left, y, S + S // 2 + 1, l.h, true)
      if kind == "fence" and #l.cells == 0 then
        for x = left + 4 * S, screen.W - M, 4 * S do screen.rect(x, y + l.h // 2, S, S, true) end
      end
    elseif not d.cont and kind == "ul" then
      local r = math.max(2, BODY.cw // 5)
      screen.circle(left + d.lead + BODY.cw // 2, y + BODY.asc - BODY.asc // 3, r, true)
    elseif not d.cont and kind == "task" then
      local bx, sz = left + d.lead, BODY.cw
      local by = y + BODY.asc - sz + S
      screen.rect(bx, by, sz, sz)
      if d.checked then
        for t = 0, 1 do
          screen.line(bx + 2, by + sz // 2 + t, bx + sz // 2 - 1, by + sz - 3 + t)
          screen.line(bx + sz // 2 - 1, by + sz - 3 + t, bx + sz - 2, by + 2 + t)
        end
      end
    elseif not d.cont and kind == "ol" then
      screen.font(FONT.body)
      screen.text(left + d.lead, y, d.num .. ".")
      screen.font()
    end
  end

  -- Text, in runs of the same font
  local x = l.x + ox
  local k = 1
  local cells = l.cells
  while k <= #cells do
    local f = fontFor(cells[k], l.font)
    local j = k
    local txt = {}
    while j <= #cells and fontFor(cells[j], l.font) == f do txt[#txt + 1] = cells[j].ch; j = j + 1 end
    screen.font(f)
    screen.text(x, y, table.concat(txt))
    local cw, asc = FM[f].cw, FM[f].asc
    for q = k, j - 1 do
      local c = cells[q]
      local cx = x + (q - k) * cw
      if c.s then screen.rect(cx, y + asc - asc * 2 // 5, cw, S, true) end
      if c.link then screen.rect(cx, y + asc + 2 * S, cw, S, true) end
      if c.code then screen.invert(cx, y, cw, l.fm.lh) end
    end
    x = x + (j - k) * cw
    k = j
  end
  screen.font()

  -- Rule under level 1 and 2 headings
  if l.underline then screen.rect(left, y + l.fm.lh + S, screen.W - M - left, S, true) end
end

-- ---------------------------------------------------------------------
--  Cursor
-- ---------------------------------------------------------------------
-- Display line and x position of the cursor in the active paragraph
local function cursorSpot()
  local lines = layout(cr)
  for n, l in ipairs(lines) do
    local cw = l.fm.cw
    for q, cell in ipairs(l.cells) do
      if cell.ri == cc + 1 then return n, l.x + (q - 1) * cw end
    end
  end
  -- end of the paragraph
  local l = lines[#lines]
  local cw = l.fm.cw
  return #lines, l.x + #l.cells * cw
end

-- Column for a pixel x on display line n of the active paragraph
local function colAt(n, x)
  local lines = layout(cr)
  local l = lines[math.max(1, math.min(n, #lines))]
  local cw = l.fm.cw
  local best = nil
  for q, cell in ipairs(l.cells) do
    local cx = l.x + (q - 1) * cw
    if cx + cw // 2 > x then best = cell.ri - 1; break end
  end
  if best then return best end
  if #l.cells == 0 then return 0 end
  local last = l.cells[#l.cells].ri
  if n < #lines then return last - 1 end        -- stay on this line (before the wrap point)
  return last
end

-- Keep the cursor on screen
local VIEW_TOP = screen.top + 2 * S
local VIEW_BOTTOM = screen.H - S
local function viewHeight() return VIEW_BOTTOM - VIEW_TOP end

local function ensureVisible()
  local line = cursorSpot()
  if cr < topPara or (cr == topPara and line < topLine) then
    topPara, topLine = cr, line
    updateTitle()
    return
  end
  -- height from the top of the view to the bottom of the cursor line
  local function heightToCursor()
    local h = 0
    local p, ln = topPara, topLine
    while true do
      local ls = layout(p)
      for n = ln, #ls do
        h = h + ls[n].h
        if p == cr and n == line then return h end
      end
      p, ln = p + 1, 1
      if p > #doc then return h end
    end
  end
  updateTitle()
  while heightToCursor() > viewHeight() do
    local ls = layout(topPara)
    if topLine < #ls then topLine = topLine + 1 else topPara, topLine = topPara + 1, 1 end
    if topPara > cr then topPara, topLine = cr, line; break end
  end
end

-- ---------------------------------------------------------------------
--  Editing
-- ---------------------------------------------------------------------
local function changed()
  dirty = true
  computeFences()
  invalidate()
  updateTitle()
end

local function insertText(t)
  local p = doc[cr]
  doc[cr] = p:sub(1, cc) .. t .. p:sub(cc + 1)
  cc = cc + #t
  wantX = nil
  changed()
end

local function newLine()
  local p = doc[cr]
  local before, after = p:sub(1, cc), p:sub(cc + 1)
  local blk = classify(cr)
  local cont = ""
  -- Continue lists and quotes; an empty item ends the list instead
  if blk.kind == "ul" or blk.kind == "task" or blk.kind == "ol" or blk.kind == "quote" then
    local body = p:sub(blk.plen + 1)
    if body:match("^%s*$") and cc >= blk.plen then
      doc[cr] = ""
      cc = 0
      changed()
      return
    end
    if cc >= blk.plen then
      local ind = string.rep(" ", blk.indent or 0)
      if blk.kind == "ul" then cont = ind .. p:match("^%s*([-*+] )")
      elseif blk.kind == "task" then cont = ind .. p:match("^%s*([-*+] )") .. "[ ] "
      elseif blk.kind == "ol" then cont = ind .. (tonumber(blk.num) + 1) .. p:match("^%s*%d+([.)] )")
      else cont = "> " end
    end
  end
  doc[cr] = before
  table.insert(doc, cr + 1, cont .. after)
  cr, cc = cr + 1, #cont
  wantX = nil
  changed()
end

local function backspace()
  if cc > 0 then
    local p = doc[cr]
    doc[cr] = p:sub(1, cc - 1) .. p:sub(cc + 1)
    cc = cc - 1
  elseif cr > 1 then
    cc = #doc[cr - 1]
    doc[cr - 1] = doc[cr - 1] .. doc[cr]
    table.remove(doc, cr)
    cr = cr - 1
  else
    return false
  end
  wantX = nil
  changed()
  return true
end

local function moveVertical(dir)
  local line, x = cursorSpot()
  wantX = wantX or x
  local lines = layout(cr)
  if dir < 0 then
    if line > 1 then cc = colAt(line - 1, wantX); return true end
    if cr == 1 then if cc == 0 then return false end; cc = 0; return true end
    cr = cr - 1
    cc = colAt(#layout(cr), wantX)
  else
    if line < #lines then cc = colAt(line + 1, wantX); return true end
    if cr == #doc then if cc == #doc[cr] then return false end; cc = #doc[cr]; return true end
    cr = cr + 1
    cc = colAt(1, wantX)
  end
  return true
end

-- ---------------------------------------------------------------------
--  Selection
-- ---------------------------------------------------------------------
local function hasSel() return anchor ~= nil and not (anchor.r == cr and anchor.c == cc) end

local function selRange()                     -- ordered: r1,c1 (start) .. r2,c2 (end, exclusive)
  local r1, c1, r2, c2 = anchor.r, anchor.c, cr, cc
  if r1 > r2 or (r1 == r2 and c1 > c2) then r1, c1, r2, c2 = r2, c2, r1, c1 end
  return r1, c1, r2, c2
end

local function inSel(r, c)                    -- is character c (0-based) of paragraph r selected?
  if not hasSel() then return false end
  local r1, c1, r2, c2 = selRange()
  if r < r1 or r > r2 then return false end
  if r == r1 and c < c1 then return false end
  if r == r2 and c >= c2 then return false end
  return true
end

local function selText()
  local r1, c1, r2, c2 = selRange()
  if r1 == r2 then return doc[r1]:sub(c1 + 1, c2) end
  local t = { doc[r1]:sub(c1 + 1) }
  for r = r1 + 1, r2 - 1 do t[#t + 1] = doc[r] end
  t[#t + 1] = doc[r2]:sub(1, c2)
  return table.concat(t, "\n")
end

local function deleteSel()
  local r1, c1, r2, c2 = selRange()
  doc[r1] = doc[r1]:sub(1, c1) .. doc[r2]:sub(c2 + 1)
  for r = r2, r1 + 1, -1 do table.remove(doc, r) end
  cr, cc = r1, c1
  anchor = nil
end

-- ---------------------------------------------------------------------
--  Undo: a snapshot of the paragraph list (strings are shared, so it's cheap)
-- ---------------------------------------------------------------------
local function snapshot()
  local d = {}
  for i, p in ipairs(doc) do d[i] = p end
  return { doc = d, cr = cr, cc = cc }
end

local function pushUndo(kind)
  if kind == "type" and lastKind == "type" then return end    -- a word is one step
  undo[#undo + 1] = snapshot()
  local limit = math.max(5, math.min(40, 20000 // math.max(1, #doc)))
  while #undo > limit do table.remove(undo, 1) end
  redo = {}
  lastKind = kind
end

local function restore(from, to)
  if #from == 0 then return false end
  to[#to + 1] = snapshot()
  local s = table.remove(from)
  doc, cr, cc = s.doc, s.cr, math.min(s.cc, #s.doc[s.cr])
  anchor, lastKind, wantX = nil, nil, nil
  return true
end

-- Insert text that may contain several lines (paste)
local function insertMulti(t)
  t = t:gsub("\r", ""):gsub("\t", "  ")
  local lines = {}
  for l in (t .. "\n"):gmatch("([^\n]*)\n") do lines[#lines + 1] = l end
  local p = doc[cr]
  local before, after = p:sub(1, cc), p:sub(cc + 1)
  if #lines == 1 then
    doc[cr] = before .. lines[1] .. after
    cc = cc + #lines[1]
  else
    doc[cr] = before .. lines[1]
    for i = 2, #lines do table.insert(doc, cr + i - 1, lines[i]) end
    cr = cr + #lines - 1
    cc = #doc[cr]
    doc[cr] = doc[cr] .. after
  end
end

-- ---------------------------------------------------------------------
--  Formatting shortcuts
-- ---------------------------------------------------------------------
-- Wrap the selection in a marker (or add an empty pair at the cursor); again removes it
local function wrapWith(m)
  local L = #m
  if hasSel() then
    local r1, c1, r2, c2 = selRange()
    if r1 ~= r2 then flash = "Select within one paragraph"; return end
    local p = doc[r1]
    -- markers must hug the words: "**and**", not "**and **"
    while c1 < c2 and p:sub(c1 + 1, c1 + 1) == " " do c1 = c1 + 1 end
    while c2 > c1 and p:sub(c2, c2) == " " do c2 = c2 - 1 end
    if c1 == c2 then flash = "Select some text first"; return end
    pushUndo("fmt")
    local inner = p:sub(c1 + 1, c2)
    if c1 >= L and p:sub(c1 - L + 1, c1) == m and p:sub(c2 + 1, c2 + L) == m then
      doc[r1] = p:sub(1, c1 - L) .. inner .. p:sub(c2 + L + 1)
      anchor, cr, cc = { r = r1, c = c1 - L }, r1, c2 - L
    else
      doc[r1] = p:sub(1, c1) .. m .. inner .. m .. p:sub(c2 + 1)
      anchor, cr, cc = { r = r1, c = c1 + L }, r1, c2 + L
    end
  else
    pushUndo("fmt")
    local p = doc[cr]
    if cc >= L and p:sub(cc - L + 1, cc) == m and p:sub(cc + 1, cc + L) == m then
      doc[cr] = p:sub(1, cc - L) .. p:sub(cc + L + 1)            -- remove an empty pair
      cc = cc - L
    else
      doc[cr] = p:sub(1, cc) .. m .. m .. p:sub(cc + 1)          -- type between the markers
      cc = cc + L
    end
  end
  changed()
end

local function setHeading(level)
  pushUndo("fmt")
  local p = doc[cr]
  local hashes, rest = p:match("^(#+) (.*)$")
  local body = hashes and rest or p
  if not hashes then                                           -- a heading replaces list/quote markers
    body = body:gsub("^%s*[-*+]%s+%[[ xX]%]%s*", ""):gsub("^%s*[-*+>]%s+", ""):gsub("^%s*%d+[.)]%s+", "")
  end
  local new = (hashes and #hashes == level) and body or (string.rep("#", level) .. " " .. body)
  cc = math.max(0, math.min(#new, cc + (#new - #p)))
  doc[cr] = new
  changed()
end

local function toggleTask()
  pushUndo("fmt")
  local p = doc[cr]
  local ind, box = p:match("^(%s*)[-*+] %[([ xX])%] ")
  if ind then
    local pos = #ind + 4                                           -- the character inside [ ]
    doc[cr] = p:sub(1, pos - 1) .. (box == " " and "x" or " ") .. p:sub(pos + 1)
  else
    local ind2, bullet = p:match("^(%s*)([-*+]) ")
    if ind2 then
      doc[cr] = ind2 .. bullet .. " [ ] " .. p:sub(#ind2 + 3)
      cc = cc + 4
    else
      doc[cr] = "- [ ] " .. p
      cc = cc + 6
    end
  end
  changed()
end

local function wordLeft()
  if cc == 0 then if cr > 1 then cr = cr - 1; cc = #doc[cr] end; return end
  local p, i = doc[cr], cc
  while i > 0 and p:sub(i, i) == " " do i = i - 1 end
  while i > 0 and p:sub(i, i) ~= " " do i = i - 1 end
  cc = i
end

local function wordRight()
  local p = doc[cr]
  if cc >= #p then if cr < #doc then cr = cr + 1; cc = 0 end; return end
  local i = cc + 1
  while i <= #p and p:sub(i, i) ~= " " do i = i + 1 end
  while i <= #p and p:sub(i, i) == " " do i = i + 1 end
  cc = i - 1
end

local function deleteForward()
  local p = doc[cr]
  if cc < #p then doc[cr] = p:sub(1, cc) .. p:sub(cc + 2)
  elseif cr < #doc then doc[cr] = p .. doc[cr + 1]; table.remove(doc, cr + 1)
  else return false end
  return true
end

-- ---------------------------------------------------------------------
--  Syntax help
-- ---------------------------------------------------------------------
local HELP = {
  { "# Heading 1",  "# Heading 1" },
  { "## Heading 2", "## Heading 2" },
  { "### Heading 3", "### Heading 3" },
  { "**bold**",     "**bold**" },
  { "*italic*",     "*italic*" },
  { "***both***",   "***both***" },
  { "~~strike~~",   "~~strike~~" },
  { "`code`",       "`code`" },
  { "[link](url)",  "[link](url)" },
  { "- item",       "- item" },
  { "1. item",      "1. item" },
  { "- [ ] to do",  "- [ ] to do" },
  { "- [x] done",   "- [x] done" },
  { "> quote",      "> quote" },
  { "---",          "---" },
  { "```",          "code block" },
}
local FORMAT_KEYS = { ["Fn+B I"] = true, ["Fn+K D"] = true, ["Fn+1 2 3"] = true, ["Fn+Enter"] = true }
local SHORTCUTS = {
  { "Tab, Fn+H",  "this help" },
  { "Fn+S",       "save" },
  { "Fn+Space",   "select on/off" },
  { "Fn+A",       "select all" },
  { "Fn+C X V",   "copy/cut/paste" },
  { "Fn+Z Y",     "undo / redo" },
  { "Fn+B I",     "bold / italic" },
  { "Fn+K D",     "code / strike" },
  { "Fn+1 2 3",   "heading 1-3" },
  { "Fn+Enter",   "checkbox" },
  { "Fn+arrows",  "word/page jump" },
  { "Shift+Bksp", "delete forward" },
  { "Esc",        "save, back to calendar" },
}
local helpPages = nil

-- Render the examples with the real renderer by laying them out as a scratch document
local function helpLayouts()
  local saved = { doc = doc, cr = cr, inCode = inCode, cache = layoutCache }
  local out = {}
  for idx, h in ipairs(HELP) do
    if h[1] == "```" then
      doc = { "code block" }; inCode = { "code" }
    else
      doc = { h[2] }; inCode = {}
    end
    cr = 0; layoutCache = {}
    out[idx] = layout(1)
  end
  doc, cr, inCode, layoutCache = saved.doc, saved.cr, saved.inCode, saved.cache
  return out
end

local function buildHelpPages()
  local rendered = PLAIN and {} or helpLayouts()
  local avail = screen.H - screen.top - 2 * S - 2 * BODY.lh      -- title + key hints
  helpPages = { {} }
  local used = 0
  for idx, ls in ipairs(rendered) do
    local h = 0
    for _, l in ipairs(ls) do h = h + math.max(l.h, BODY.lh) end
    if used + h > avail and #helpPages[#helpPages] > 0 then helpPages[#helpPages + 1] = {}; used = 0 end
    local pg = helpPages[#helpPages]
    pg[#pg + 1] = { idx = idx, lines = ls, h = h }
    used = used + h
  end
  -- Keyboard shortcuts start on their own page
  if #helpPages[#helpPages] > 0 then helpPages[#helpPages + 1] = {} end
  local pg0 = helpPages[#helpPages]
  pg0[1] = { label = "Tap Fn, then the key:", h = BODY.lh }
  used = BODY.lh
  for _, sc in ipairs(SHORTCUTS) do
    if PLAIN and FORMAT_KEYS[sc[1]] then goto skip end
    if used + BODY.lh > avail then helpPages[#helpPages + 1] = {}; used = 0 end
    local pg = helpPages[#helpPages]
    pg[#pg + 1] = { shortcut = sc, h = BODY.lh }
    used = used + BODY.lh
    ::skip::
  end
end

-- ---------------------------------------------------------------------
--  Callbacks
-- ---------------------------------------------------------------------
function init()
  file.mkdir(DOCS)
  scanFiles()
end

function key(k, ch, fn)
  message = nil

  if mode == "list" then
    local n = #files + 1                     -- +1 = "New document"
    if k == keys.UP then fsel = fsel > 1 and fsel - 1 or n
    elseif k == keys.DOWN then fsel = fsel < n and fsel + 1 or 1
    elseif k == keys.ENTER then
      if fsel == 1 then openDoc(nil) else openDoc(files[fsel - 1].name) end
    elseif k == keys.BKSP and fsel > 1 then delYes = false; mode = "delete"
    elseif (ch == "r" or fn == "r") and fsel > 1 then
      renameFrom = files[fsel - 1].name
      renameBack = "list"
      nameText, nameWarn = renameFrom, nil
      mode = "rename"
    else return false end
    return
  end

  if mode == "rename" then
    if k == keys.ENTER then
      local name = nameText:gsub("^%s+", ""):gsub("%s+$", ""):gsub(" ", "_")
      if name == "" then return false end
      if not name:lower():match("%.md$") and not name:lower():match("%.txt$") then name = name .. EXT end
      if name ~= renameFrom then
        if file.exists(DOCS .. "/" .. name) then nameWarn = name; return end
        if file.rename(DOCS .. "/" .. renameFrom, DOCS .. "/" .. name) then
          if fileName == renameFrom then fileName = name end
          message = "Renamed to " .. name
          flash = message
        else
          message = "Couldn't rename"
        end
      end
      scanFiles()
      for i, f in ipairs(files) do if f.name == name then fsel = i + 1 end end
      mode = renameBack
      updateTitle()
      if mode == "edit" then screen.redraw(true) end
    elseif k == keys.BKSP then nameText = nameText:sub(1, -2); nameWarn = nil
    elseif ch and ch:match("[%w%-_%. ]") and #nameText < 40 then nameText = nameText .. ch; nameWarn = nil
    else return false end
    return
  end

  if mode == "delete" then
    if k == keys.LEFT or k == keys.RIGHT or k == keys.UP or k == keys.DOWN then delYes = not delYes
    elseif ch == "y" then delYes = true
    elseif ch == "n" then delYes = false
    elseif k == keys.ENTER then
      if delYes then
        file.remove(DOCS .. "/" .. files[fsel - 1].name)
        message = "Deleted " .. files[fsel - 1].name
        scanFiles()
      end
      mode = "list"
    else return false end
    return
  end

  if mode == "help" then
    if k == keys.TAB then mode = "edit"; screen.redraw(true)
    elseif k == keys.DOWN or k == keys.RIGHT or k == keys.ENTER or ch == " " then
      helpPage = helpPage % #helpPages + 1
    elseif k == keys.UP or k == keys.LEFT then
      helpPage = (helpPage - 2) % #helpPages + 1
    else return false end
    return
  end

  if mode == "save" then
    if k == keys.UP then saveSel = saveSel > 1 and saveSel - 1 or 3
    elseif k == keys.DOWN then saveSel = saveSel < 3 and saveSel + 1 or 1
    elseif ch == "y" or ch == "s" then saveSel = 1; k = keys.ENTER
    elseif ch == "n" or ch == "d" then saveSel = 2; k = keys.ENTER end
    if k == keys.ENTER then
      if saveSel == 1 then
        nameText = fileName or suggestName()
        nameWarn = nil
        afterName = "close"
        mode = "name"
      elseif saveSel == 2 then
        closeDoc()
      else
        mode = "edit"
      end
    elseif not (k == keys.UP or k == keys.DOWN) then
      return false
    end
    return
  end

  if mode == "name" then
    if k == keys.ENTER then
      local name = nameText:gsub("^%s+", ""):gsub("%s+$", ""):gsub(" ", "_")
      if name == "" then return false end
      if not name:lower():match("%.md$") and not name:lower():match("%.txt$") then name = name .. EXT end
      if name ~= fileName and file.exists(DOCS .. "/" .. name) and nameWarn ~= name then
        nameWarn = name                      -- press Enter again to overwrite
        return
      end
      if saveDoc(name) then
        if afterName == "close" then closeDoc()
        else mode = "edit"; flash = "Saved " .. name; updateTitle(); screen.redraw(true) end
      else
        mode = "edit"
      end
    elseif k == keys.BKSP then nameText = nameText:sub(1, -2); nameWarn = nil
    elseif ch and ch:match("[%w%-_%. ]") and #nameText < 40 then nameText = nameText .. ch; nameWarn = nil
    else return false end
    return
  end

  -- ---- editing ----
  if flash then flash = nil; updateTitle() end

  -- Fn shortcuts (tap Fn, then the key)
  if fn then
    if fn == "h" then k = keys.TAB                                  -- same as Tab: help
    elseif fn == "space" then
      if anchor then anchor = nil else anchor = { r = cr, c = cc } end
      updateTitle()
      return
    elseif fn == "a" then
      anchor = { r = 1, c = 0 }
      cr, cc = #doc, #doc[#doc]
      ensureVisible()
      return
    elseif fn == "c" or fn == "x" then
      if not hasSel() then flash = "Select first: Fn+Space"; updateTitle(); return end
      local t = selText()
      sys.clipboard(t)
      if fn == "x" then pushUndo("cut"); deleteSel(); changed()
      else anchor = nil end
      flash = (fn == "x" and "Cut " or "Copied ") .. #t .. " chars"
      updateTitle()
      ensureVisible()
      return
    elseif fn == "v" then
      local t = sys.clipboard()
      if t == "" then flash = "Clipboard is empty"; updateTitle(); return false end
      pushUndo("paste")
      if hasSel() then deleteSel() end
      anchor = nil
      insertMulti(t)
      lastKind = nil
      changed()
      ensureVisible()
      return
    elseif fn == "z" or fn == "y" then
      local ok = fn == "z" and restore(undo, redo) or (fn == "y" and restore(redo, undo))
      if not ok then flash = fn == "z" and "Nothing to undo" or "Nothing to redo"; updateTitle(); return end
      dirty = true
      computeFences(); invalidate(); updateTitle()
      ensureVisible()
      return
    elseif fn == "s" then
      if fileName then
        if saveDoc(fileName) then flash = "Saved" end
        updateTitle()
      else
        nameText = suggestName(); nameWarn = nil
        afterName = "stay"
        mode = "name"
      end
      return
    elseif fn == "r" then
      if not fileName then                                    -- never saved: same as Fn+S
        nameText = suggestName(); nameWarn = nil
        afterName = "stay"
        mode = "name"
      else
        renameFrom, renameBack = fileName, "edit"
        nameText, nameWarn = fileName, nil
        mode = "rename"
      end
      return
    elseif PLAIN and (fn == "b" or fn == "i" or fn == "k" or fn == "d" or fn == "1" or fn == "2" or fn == "3" or fn == "enter") then
      return false                                            -- no formatting in plain text
    elseif fn == "b" then wrapWith("**"); ensureVisible(); return
    elseif fn == "i" then wrapWith("*"); ensureVisible(); return
    elseif fn == "k" then wrapWith("`"); ensureVisible(); return
    elseif fn == "d" then wrapWith("~~"); ensureVisible(); return
    elseif fn == "1" or fn == "2" or fn == "3" then setHeading(tonumber(fn)); anchor = nil; ensureVisible(); return
    elseif fn == "enter" then toggleTask(); anchor = nil; ensureVisible(); return
    elseif fn == "left" then wordLeft(); wantX = nil; lastKind = nil; ensureVisible(); return
    elseif fn == "right" then wordRight(); wantX = nil; lastKind = nil; ensureVisible(); return
    elseif fn == "up" or fn == "down" then
      local steps = math.max(1, viewHeight() // BODY.lh - 1)
      for _ = 1, steps do moveVertical(fn == "up" and -1 or 1) end
      lastKind = nil
      ensureVisible()
      return
    elseif fn == "del" then k = keys.DEL
    else return false end
  end

  if k == keys.TAB then
    if not helpPages then buildHelpPages() end
    helpPage = 1
    mode = "help"
    screen.redraw(true)
    return
  end

  -- Typing over a selection replaces it
  local editing = k == keys.ENTER or k == keys.BKSP or k == keys.DEL or ch ~= nil
  if editing and hasSel() then
    pushUndo("sel")
    deleteSel()
    changed()
    if k == keys.BKSP or k == keys.DEL then ensureVisible(); return end
  end
  if editing and not hasSel() then anchor = nil end

  local moved = true
  if k == keys.LEFT then
    if cc > 0 then cc = cc - 1 elseif cr > 1 then cr = cr - 1; cc = #doc[cr] else moved = false end
    wantX = nil; lastKind = nil
  elseif k == keys.RIGHT then
    if cc < #doc[cr] then cc = cc + 1 elseif cr < #doc then cr = cr + 1; cc = 0 else moved = false end
    wantX = nil; lastKind = nil
  elseif k == keys.UP then moved = moveVertical(-1); lastKind = nil
  elseif k == keys.DOWN then moved = moveVertical(1); lastKind = nil
  elseif k == keys.ENTER then pushUndo("line"); newLine()
  elseif k == keys.BKSP then
    pushUndo("del")
    moved = backspace()
  elseif k == keys.DEL then
    pushUndo("del")
    moved = deleteForward()
    if moved then changed() end
  elseif ch then
    pushUndo(ch == " " and "space" or "type")
    insertText(ch)
  else return false end
  if not moved and not anchor then return false end
  ensureVisible()
end

function back()
  if mode == "list" then return false end
  if mode == "edit" then
    if anchor then anchor = nil; updateTitle(); return true end   -- Esc cancels a selection
    if dirty then saveSel = 1; mode = "save" else closeDoc() end
  elseif mode == "help" then mode = "edit"; screen.redraw(true)
  elseif mode == "save" then mode = "edit"
  elseif mode == "name" then mode = afterName == "stay" and "edit" or "save"
  elseif mode == "rename" then mode = renameBack
  elseif mode == "delete" then mode = "list"
  end
  return true
end

-- ---------------------------------------------------------------------
--  Screens
-- ---------------------------------------------------------------------
local function fit(s, n) return #s <= n and s or s:sub(1, n - 1) .. "~" end

local function header(title)
  screen.text(M, screen.top + 2 * S, title)
  screen.rect(0, screen.top + screen.lh + S, screen.W, S, true)
  return screen.top + screen.lh + 3 * S
end

local function footer(hint) screen.text(M, screen.H - screen.lh + S, fit(hint, screen.cols)) end

local function drawFileList()
  local y0 = header(LIST_TITLE)
  local items = { NEW_LABEL }
  for _, f in ipairs(files) do
    local kb = f.size < 1024 and (f.size .. " B") or string.format("%.1f KB", f.size / 1024)
    local name = fit(f.name, screen.cols - #kb - 3)
    items[#items + 1] = name .. string.rep(" ", screen.cols - 1 - #name - #kb) .. kb
  end
  local rows = (screen.H - screen.lh - y0) // screen.lh - (message and 1 or 0)
  if fsel < ftop then ftop = fsel end
  if fsel >= ftop + rows then ftop = fsel - rows + 1 end
  for r = 0, rows - 1 do
    local it = items[ftop + r]
    if not it then break end
    local y = y0 + r * screen.lh
    screen.text(M, y + S, it)
    if ftop + r == fsel then screen.invert(0, y, screen.W, screen.lh) end
  end
  if message then screen.text(M, screen.H - 2 * screen.lh + S, fit(message, screen.cols)) end
  footer("Enter=open  r=rename  Bksp=delete")
end

local function drawEditor()
  local y = VIEW_TOP
  local bottom = VIEW_BOTTOM
  local p, ln = topPara, topLine
  local cy, ch = nil, nil
  local curLine, curX = cursorSpot()
  while p <= #doc and y < bottom do
    local ls = layout(p)
    for n = ln, #ls do
      local l = ls[n]
      if y + l.h > bottom then y = bottom; break end
      drawLine(l, y)
      if anchor then                                     -- highlight the selection
        local cw = l.fm.cw
        for q, cell in ipairs(l.cells) do
          if cell.ri and inSel(p, cell.ri - 1) then screen.invert(l.x + (q - 1) * cw, y, cw, l.fm.lh) end
        end
        if #doc[p] == 0 and hasSel() then                -- a selected blank line
          local r1, c1, r2 = selRange()
          if p >= r1 and p < r2 and not (p == r1 and c1 > 0) then screen.invert(l.x, y, cw, l.fm.lh) end
        end
      end
      if p == cr and n == curLine then cy, ch = y, l.fm.lh end
      y = y + l.h
    end
    p, ln = p + 1, 1
  end
  if cy then screen.rect(curX, cy, math.max(2, 2 * S), ch, true) end   -- I-beam cursor
end

local function drawHelp()
  local y = header(HELP_TITLE .. "  " .. helpPage .. "/" .. #helpPages)
  local colX = 14 * BODY.cw + M              -- rendered examples start here (after "### Heading 3")
  for _, item in ipairs(helpPages[helpPage]) do
    screen.font(FONT.body)
    if item.label then
      screen.text(M, y, item.label)
      screen.font()
      y = y + item.h
      goto continue
    end
    if item.shortcut then
      screen.font(FONT.b)
      screen.text(M, y, item.shortcut[1])
      screen.font(FONT.body)
      screen.text(M + 11 * BODY.cw, y, item.shortcut[2])
      screen.font()
      y = y + item.h
      goto continue
    end
    screen.text(M, y, HELP[item.idx][1])
    screen.font()
    local yy = y
    for _, l in ipairs(item.lines) do
      drawLine(l, yy, colX)
      yy = yy + math.max(l.h, BODY.lh)
    end
    y = y + item.h
    ::continue::
  end
  footer("Up/Down=more  Tab=back to writing")
end

local function drawSave()
  local y0 = header("Save changes?")
  local opts = { "Save", "Don't save", "Keep editing" }
  for i, o in ipairs(opts) do
    local y = y0 + screen.lh // 2 + (i - 1) * (screen.lh + 2 * S)
    screen.text(3 * M, y + S, o)
    if i == saveSel then screen.invert(2 * M, y, screen.W - 4 * M, screen.lh) end
  end
  footer("Enter=choose  Esc=back")
end

local function drawName()
  local y0 = header(mode == "rename" and ("Rename " .. fit(renameFrom or "", screen.cols - 8))
                    or (fileName and "Save as" or "Name this document"))
  screen.text(M, y0 + screen.lh // 2, "File name:")
  local boxY = y0 + screen.lh * 3 // 2 + 2 * S
  local boxH = screen.lh + 4 * S
  screen.rect(M, boxY, screen.W - 2 * M, boxH)
  local shown = nameText
  local maxc = (screen.W - 4 * M) // screen.cw - 1
  if #shown > maxc then shown = shown:sub(-maxc) end
  screen.text(2 * M, boxY + 3 * S, shown)
  screen.rect(2 * M + #shown * screen.cw, boxY + 3 * S, 5 * S, screen.ch, true)
  local y = boxY + boxH + screen.lh // 2
  if nameWarn then
    screen.text(M, y, fit(nameWarn .. " exists.", screen.cols))
    screen.text(M, y + screen.lh, mode == "rename" and "Pick another name." or "Enter again to replace it.")
  else
    screen.text(M, y, EXT .. " is added if you leave it off")
  end
  footer(mode == "rename" and "Enter=rename  Esc=cancel" or "Enter=save  Esc=back")
end

local function drawDelete()
  local y0 = header("Delete document?")
  screen.text(M, y0 + screen.lh, fit(files[fsel - 1].name, screen.cols))
  local y = y0 + 3 * screen.lh
  local w = 6 * screen.cw
  local yesX, noX = screen.W // 4 - w // 2, screen.W * 3 // 4 - w // 2
  screen.text(yesX + screen.cw * 3 // 2, y + 2 * S, "YES")
  screen.text(noX + screen.cw * 2, y + 2 * S, "NO")
  screen.rect(yesX, y, w, screen.lh + 2 * S)
  screen.rect(noX, y, w, screen.lh + 2 * S)
  if delYes then screen.invert(yesX, y, w, screen.lh + 2 * S) else screen.invert(noX, y, w, screen.lh + 2 * S) end
  footer("Left/Right=choose  Enter=confirm")
end

function draw()
  if     mode == "list"   then drawFileList()
  elseif mode == "edit"   then drawEditor()
  elseif mode == "help"   then drawHelp()
  elseif mode == "save"   then drawSave()
  elseif mode == "name" or mode == "rename" then drawName()
  elseif mode == "delete" then drawDelete()
  end
end

-- =====================================================================
--  Journal: the calendar in front of the editor
--  (added to the shared editor code above; "list" mode is the calendar)
-- =====================================================================
local MONTHS = { "January", "February", "March", "April", "May", "June", "July",
                 "August", "September", "October", "November", "December" }
local WDAYS = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" }

local function isLeap(y) return (y % 4 == 0 and y % 100 ~= 0) or y % 400 == 0 end
local function daysIn(y, m)
  local n = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
  return (m == 2 and isLeap(y)) and 29 or n[m]
end
local function weekday(y, m, d)                -- 1 = Sunday ... 7 = Saturday
  local t = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }
  if m < 3 then y = y - 1 end
  return (y + y // 4 - y // 100 + y // 400 + t[m] + d) % 7 + 1
end
local function dateKey(y, m, d) return string.format("%04d-%02d-%02d", y, m, d) end
local function longDate(y, m, d) return WDAYS[weekday(y, m, d)] .. ", " .. d .. " " .. MONTHS[m] .. " " .. y end
local function shortDate(y, m, d) return WDAYS[weekday(y, m, d)]:sub(1, 3) .. " " .. d .. " " .. MONTHS[m]:sub(1, 3) end

local function today()
  local s = sys.date("%Y-%m-%d")
  if not s then return nil end
  local y, m, d = s:match("(%d+)-(%d+)-(%d+)")
  return tonumber(y), tonumber(m), tonumber(d)
end

local cy, cm, cd = 2026, 1, 1                  -- the selected day
local entries = {}                             -- "YYYY-MM-DD" -> true
local delPending = false
local calMsg = nil

local function scanEntries()
  entries = {}
  for _, e in ipairs(file.list(DOCS)) do
    local k = (not e.dir) and e.name:match("^(%d%d%d%d%-%d%d%-%d%d)%.md$")
    if k then entries[k] = true end
  end
end

local function monthTitle() sys.title(MONTHS[cm] .. " " .. cy) end

local function addDays(n)
  cd = cd + n
  while cd < 1 do
    cm = cm - 1
    if cm < 1 then cm = 12; cy = cy - 1 end
    cd = cd + daysIn(cy, cm)
  end
  while cd > daysIn(cy, cm) do
    cd = cd - daysIn(cy, cm)
    cm = cm + 1
    if cm > 12 then cm = 1; cy = cy + 1 end
  end
end

local function addMonths(n)
  cm = cm + n
  while cm < 1 do cm = cm + 12; cy = cy - 1 end
  while cm > 12 do cm = cm - 12; cy = cy + 1 end
  cd = math.min(cd, daysIn(cy, cm))
end

-- The first line of an entry that isn't the date heading, without markdown marks
local function preview(k)
  local text = file.read(DOCS .. "/" .. k .. ".md")
  if not text then return nil end
  for line in (text .. "\n"):gmatch("([^\n]*)\n") do
    if line:match("%S") and not line:match("^#") then
      local s = line:gsub("^%s*[-*+>]%s+%[?[ xX]?%]?%s*", ""):gsub("^%s*%d+[.)]%s+", "")
      s = s:gsub("[*_~`]", ""):gsub("%[([^%]]*)%]%([^)]*%)", "%1")
      return s
    end
  end
  return ""
end

-- ---------------------------------------------------------------------
--  Opening and closing a day
-- ---------------------------------------------------------------------
local function openDay()
  local k = dateKey(cy, cm, cd)
  if entries[k] then
    if openDoc(k .. ".md") == false then calMsg = message; return end
    return
  end
  -- A new day: the date as a heading, the cursor ready underneath
  openDoc(nil)
  fileName = k .. ".md"
  doc = { "# " .. longDate(cy, cm, cd), "", "" }
  cr, cc = 3, 0
  topPara, topLine = 1, 1
  computeFences()
  invalidate()
  dirty = false                                -- only the heading so far: nothing to save yet
  updateTitle()
end

-- An entry that's just the date heading (or blank) doesn't count
local function isEmptyEntry()
  for i, p in ipairs(doc) do
    if p:match("%S") and not (i == 1 and p:match("^#")) then return false end
  end
  return true
end

local function closeDay()
  local k = fileName:gsub("%.md$", "")
  if isEmptyEntry() then
    if entries[k] then file.remove(DOCS .. "/" .. fileName); calMsg = "Entry removed (it was empty)" end
  elseif dirty then
    if saveDoc(fileName) then calMsg = "Saved" else calMsg = message end
  end
  mode = "list"
  scanEntries()
  monthTitle()
  screen.redraw(true)
end

-- ---------------------------------------------------------------------
--  Calendar keys
-- ---------------------------------------------------------------------
local function calendarKey(k, ch, fn)
  local was = delPending
  delPending = false
  calMsg = nil
  message = nil
  if fn == "left" then addMonths(-1)
  elseif fn == "right" then addMonths(1)
  elseif fn == "up" then addMonths(-12)
  elseif fn == "down" then addMonths(12)
  elseif k == keys.LEFT then addDays(-1)
  elseif k == keys.RIGHT then addDays(1)
  elseif k == keys.UP then addDays(-7)
  elseif k == keys.DOWN then addDays(7)
  elseif ch == "t" or ch == "T" then
    local y, m, d = today()
    if not y then calMsg = "The clock isn't set yet"; return end
    cy, cm, cd = y, m, d
  elseif k == keys.ENTER or ch == " " then openDay(); return
  elseif k == keys.BKSP then
    local key = dateKey(cy, cm, cd)
    if not entries[key] then return false end
    if was then
      file.remove(DOCS .. "/" .. key .. ".md")
      scanEntries()
      calMsg = "Entry deleted"
    else
      delPending = true
      calMsg = "Bksp again to delete this entry"
    end
  else
    delPending = was
    return false
  end
  monthTitle()
end

-- ---------------------------------------------------------------------
--  Drawing the month
-- ---------------------------------------------------------------------
local function drawCalendar()
  local first = weekday(cy, cm, 1)
  local ndays = daysIn(cy, cm)
  local weeks = (first - 1 + ndays + 6) // 7
  local cellW = screen.W // 7
  local x0 = (screen.W - cellW * 7) // 2

  -- weekday names
  local y = screen.top + 2 * S
  for i = 1, 7 do
    local name = WDAYS[i]:sub(1, 3)
    local w = screen.textwidth(name)
    screen.text(x0 + (i - 1) * cellW + (cellW - w) // 2, y, name)
  end
  y = y + screen.lh
  local gridTop = y
  local bottomLines = 1
  local rowH = (screen.H - gridTop - bottomLines * screen.lh - 2 * S) // weeks
  if rowH > 40 * S then rowH = 40 * S end
  local gridH = rowH * weeks

  local ty, tm, td = today()

  -- grid lines, like the classic Calendar desk accessory
  for r = 0, weeks do screen.rect(x0, gridTop + r * rowH, cellW * 7, S, true) end
  for c = 0, 7 do screen.rect(x0 + c * cellW - (c == 7 and S or 0), gridTop, S, gridH, true) end

  for d = 1, ndays do
    local i = first - 1 + d - 1
    local cx = x0 + (i % 7) * cellW
    local cyy = gridTop + (i // 7) * rowH
    local num = tostring(d)
    screen.text(cx + 3 * S, cyy + 3 * S, num)
    if entries[dateKey(cy, cm, d)] then                     -- entry indicator: a dot after the number
      local r = math.max(2, 2 * S)
      screen.circle(cx + 3 * S + screen.textwidth(num) + 2 * S + r, cyy + 3 * S + screen.ch // 2, r, true)
    end
    if ty == cy and tm == cm and td == d then               -- today: a heavy outline
      screen.rect(cx + 2 * S, cyy + 2 * S, cellW - 3 * S, rowH - 3 * S)
      screen.rect(cx + 3 * S, cyy + 3 * S, cellW - 5 * S, rowH - 5 * S)
    end
    if d == cd then screen.invert(cx + S, cyy + S, cellW - S, rowH - S) end
  end

  -- the selected day, and the start of its entry
  local line
  if calMsg or message then
    line = calMsg or message
  else
    local k = dateKey(cy, cm, cd)
    if entries[k] then
      local p = preview(k) or ""
      line = shortDate(cy, cm, cd) .. ": " .. (p ~= "" and p or "(heading only)")
    else
      line = shortDate(cy, cm, cd) .. ": Enter to write"
    end
  end
  if #line > screen.cols then line = line:sub(1, screen.cols - 1) .. "~" end
  screen.text(M, screen.H - screen.lh + S, line)
end

-- ---------------------------------------------------------------------
--  Hooking the calendar in front of the editor
-- ---------------------------------------------------------------------
local _init, _key, _back, _draw = init, key, back, draw

function init()
  _init()
  local y, m, d = today()
  if y then
    cy, cm, cd = y, m, d
  else                                         -- no clock yet: start at the newest entry
    local newest
    for _, e in ipairs(file.list(DOCS)) do
      local k = e.name:match("^(%d%d%d%d%-%d%d%-%d%d)%.md$")
      if k and (not newest or k > newest) then newest = k end
    end
    if newest then
      local ny, nm, nd = newest:match("(%d+)-(%d+)-(%d+)")
      cy, cm, cd = tonumber(ny), tonumber(nm), tonumber(nd)
    end
    calMsg = "Clock not set: showing " .. MONTHS[cm] .. " " .. cy
  end
  scanEntries()
  monthTitle()
end

function key(k, ch, fn)
  if mode == "list" then return calendarKey(k, ch, fn) end
  if mode == "edit" and fn == "r" then return false end      -- entries are named by date: no renaming
  return _key(k, ch, fn)
end

function back()
  if mode == "list" then return false end                      -- leave the app
  if mode == "edit" and not anchor then closeDay(); return true end   -- Esc saves and goes back
  return _back()
end

function draw()
  if mode == "list" then drawCalendar() else _draw() end
end
