-- =====================================================================
--  Checklist — multiple checklists, saved as markdown task lists
--
--  Each list is lists/<name>.md with lines like "- [ ] Milk" / "- [x] Eggs",
--  so lists also open in the Markdown app and the Uploader's Checklists tab.
--  Other lines (e.g. "## Produce") show as section labels and are kept.
--
--  Checked items sink below the unchecked ones (within their section), with
--  the most recently checked on top. Unchecking puts an item back where it was.
--
--  Lists screen:  Up/Down, Enter = open, r = rename, Bksp = delete, Esc = exit
--  A list:        x or Space = check / uncheck, Enter = edit, a = add item,
--                 Fn+Up/Down = move item, Bksp twice = delete item,
--                 c twice = clear checked items, Tab = help, Esc = back
--  Editing:       type, Enter = next item, Esc (or Enter on an empty item) = done
-- =====================================================================

-- true: pressing x while typing finishes editing (then items can't contain an x)
local X_FINISHES_EDIT = false

local LISTS = "lists"
local S, M = screen.scale, screen.margin
local FONT, FONTB = "mono9", "mono9b"
local CW, LH, ASC = screen.font(FONT)
screen.font()
local ROW = LH + 2 * S                         -- height of one item row

-- ---------------------------------------------------------------------
--  State
-- ---------------------------------------------------------------------
local mode = "lists"        -- lists | list | edit | help | name | delete
local lists, lsel, ltop = {}, 1, 1
local message = nil

local cur = nil             -- open list: { file = "Groceries.md", entries = {...} }
local sel, top = 1, 1       -- selected row (entries, then the "+ Add item" row)
local pending = nil         -- "delete" / "clear": waiting for the second press

local editIdx, editText, editPos = nil, "", 0
local nameText, nameFor, nameWarn = "", nil, nil   -- nameFor: nil = new list, else the file being renamed
local delYes = false

-- ---------------------------------------------------------------------
--  Files
-- ---------------------------------------------------------------------
local function titleOf(file)
  local t = file:gsub("%.md$", ""):gsub("_", " ")
  return t
end

local function parse(text)
  local e = {}
  for line in (text .. "\n"):gmatch("([^\n]*)\n") do
    line = line:gsub("\r", "")
    local box, rest = line:match("^%s*[-*+] %[([ xX])%]%s?(.*)$")
    if box then e[#e + 1] = { item = true, done = box ~= " ", text = rest }
    elseif line:match("%S") then e[#e + 1] = { item = false, raw = line } end
  end
  return e
end

local function serialize(entries)
  local out = {}
  for _, x in ipairs(entries) do
    out[#out + 1] = x.item and ("- [" .. (x.done and "x" or " ") .. "] " .. x.text) or x.raw
  end
  return table.concat(out, "\n") .. "\n"
end

local function counts(entries)
  local done, total = 0, 0
  for _, x in ipairs(entries) do
    if x.item then total = total + 1; if x.done then done = done + 1 end end
  end
  return done, total
end

local function scanLists()
  lists = {}
  for _, e in ipairs(file.list(LISTS)) do
    if not e.dir and e.name:lower():match("%.md$") then
      local text = file.read(LISTS .. "/" .. e.name)
      local d, t = counts(parse(text or ""))
      lists[#lists + 1] = { file = e.name, title = titleOf(e.name), done = d, total = t, unreadable = not text }
    end
  end
  table.sort(lists, function(a, b) return a.title:lower() < b.title:lower() end)
  if lsel > #lists + 1 then lsel = #lists + 1 end
end

local function save()
  if cur then file.write(LISTS .. "/" .. cur.file, serialize(cur.entries)) end
end

local function fileNameFor(title)
  local base = title:gsub("^%s+", ""):gsub("%s+$", ""):gsub("%s+", "_"):gsub("[^%w_%-']", "")
  if base == "" then return nil end
  return base .. ".md"
end

-- ---------------------------------------------------------------------
--  Ordering: in each section (items between headings), unchecked items
--  come first, then checked items, most recently checked first
-- ---------------------------------------------------------------------
local function sectionOf(i)                        -- first and last index of item i's section
  local e = cur.entries
  local a, b = i, i
  while a > 1 and e[a - 1].item do a = a - 1 end
  while b < #e and e[b + 1].item do b = b + 1 end
  return a, b
end

local function firstChecked(a, b)                  -- where the checked group starts (b + 1 if none)
  for j = a, b do if cur.entries[j].done then return j end end
  return b + 1
end

-- Check or uncheck item i, moving it into place. Returns its new index.
local function toggle(i)
  local e = cur.entries[i]
  local a, b = sectionOf(i)
  table.remove(cur.entries, i)
  local fc = firstChecked(a, b - 1)
  local pos
  if not e.done then
    e.done = true
    -- remember its neighbours, so unchecking can put it back exactly
    local prev, nxt = cur.entries[i - 1], cur.entries[i]
    e.after  = (i > a and prev and prev.item and not prev.done) and prev or false   -- false = it was first
    e.before = (i < fc and nxt and nxt.item and not nxt.done) and nxt or nil
    pos = fc                                       -- top of the checked group
  else
    e.done = false
    local function find(x) for j = a, fc - 1 do if cur.entries[j] == x then return j end end end
    local b4, af = e.before and find(e.before), e.after and find(e.after)
    if b4 then pos = b4                            -- back in front of the item that followed it
    elseif af then pos = af + 1                    -- or after the one that preceded it
    elseif e.after == false then pos = a           -- it was first
    else pos = fc end                              -- unknown (list reopened): bottom of the unchecked items
    e.after, e.before = nil, nil
  end
  table.insert(cur.entries, pos, e)
  return pos
end

-- Put checked items below unchecked ones in every section (keeps their order)
local function normalize()
  local e, out, i, changed = cur.entries, {}, 1, false
  while i <= #e do
    if not e[i].item then
      out[#out + 1] = e[i]; i = i + 1
    else
      local open, done = {}, {}
      while i <= #e and e[i].item do
        if e[i].done then done[#done + 1] = e[i] else open[#open + 1] = e[i] end
        i = i + 1
      end
      for _, x in ipairs(open) do out[#out + 1] = x end
      for _, x in ipairs(done) do out[#out + 1] = x end
    end
  end
  for j = 1, #e do if out[j] ~= e[j] then changed = true; break end end
  cur.entries = out
  return changed
end

-- Where a new (unchecked) item goes when adding "below" row i
local function addPosition(i)
  local e = cur.entries
  if i > #e then                                   -- "+ Add item": end of the last section's unchecked items
    if #e > 0 and e[#e].item then
      local a, b = sectionOf(#e)
      return firstChecked(a, b)
    end
    return #e + 1
  end
  if not e[i].done then return i + 1 end
  local a, b = sectionOf(i)
  return firstChecked(a, b)                        -- below a checked item: join the unchecked ones
end

-- ---------------------------------------------------------------------
--  Rows in an open list: every entry, then "+ Add item"
-- ---------------------------------------------------------------------
local function rowCount() return #cur.entries + 1 end
local function isAddRow(i) return i == #cur.entries + 1 end
local function selectable(i) return isAddRow(i) or cur.entries[i].item end

local function moveSel(dir)
  local i = sel
  repeat i = i + dir until i < 1 or i > rowCount() or selectable(i)
  if i >= 1 and i <= rowCount() then sel = i; return true end
  return false
end

local function openList(f)
  -- Never open a list that couldn't be read in full: saving it would wipe the file
  local text, err = file.read(LISTS .. "/" .. f)
  if not text then
    message = (err or "can't read the file"):gsub("^%l", string.upper)   -- fits one line
    return false
  end
  cur = { file = f, entries = parse(text) }
  if normalize() then file.write(LISTS .. "/" .. f, serialize(cur.entries)) end
  sel, top, pending = 1, 1, nil
  if not selectable(sel) then moveSel(1) end
  mode = "list"
  sys.title(titleOf(f))
  screen.redraw(true)
end

local function closeList()
  save()
  cur = nil
  mode = "lists"
  sys.title("")
  scanLists()
  screen.redraw(true)
end

-- ---------------------------------------------------------------------
--  Editing an item
-- ---------------------------------------------------------------------
local function startEdit(i)
  editIdx = i
  editText = cur.entries[i].text
  editPos = #editText
  mode = "edit"
end

local function startAdd(at)                        -- insert an empty item at position `at`
  table.insert(cur.entries, at, { item = true, done = false, text = "" })
  sel = at
  startEdit(at)
end

-- Finish editing: an empty item is removed. Returns true if the item was kept.
local function finishEdit()
  local text = editText:gsub("^%s+", ""):gsub("%s+$", "")
  local kept = text ~= ""
  if kept then
    cur.entries[editIdx].text = text
  else
    table.remove(cur.entries, editIdx)
    if sel > rowCount() then sel = rowCount() end
    if not selectable(sel) and not moveSel(-1) then moveSel(1) end
  end
  editIdx = nil
  mode = "list"
  save()
  return kept
end

-- ---------------------------------------------------------------------
--  Help contents (defined before the key handlers, which page through it)
-- ---------------------------------------------------------------------
local HELP = {
  { "x, Space",   "check / uncheck" },
  { "Enter",      "edit the item" },
  { "a",          "add an item below" },
  { "Fn+Up/Down", "move the item" },
  { "Bksp twice", "delete the item" },
  { "c twice",    "clear checked items" },
  { "Esc",        "back to your lists" },
  { "Typing:",    "" },
  { " Enter",     "next item; twice=done" },
  { " Esc",       "done" },
}
local helpPage = 1

local function helpRows()
  return math.max(1, (screen.H - screen.lh - (screen.top + screen.lh + 3 * S)) // LH)
end

-- ---------------------------------------------------------------------
--  Keys
-- ---------------------------------------------------------------------
local function listsKey(k, ch, fn)
  local n = #lists + 1                             -- +1 = "+ New checklist"
  if k == keys.UP then lsel = lsel > 1 and lsel - 1 or n
  elseif k == keys.DOWN then lsel = lsel < n and lsel + 1 or 1
  elseif k == keys.ENTER then
    if lsel == 1 then nameText, nameFor, nameWarn = "", nil, nil; mode = "name"
    else openList(lists[lsel - 1].file) end
  elseif (ch == "r" or fn == "r") and lsel > 1 then
    nameFor = lists[lsel - 1].file
    nameText, nameWarn = titleOf(nameFor), nil
    mode = "name"
  elseif k == keys.BKSP and lsel > 1 then delYes = false; mode = "delete"
  else return false end
end

local function listKey(k, ch, fn)
  local was = pending
  pending = nil
  local e = cur.entries[sel]

  if ch == "x" or ch == "X" or ch == " " then
    if not (e and e.item) then return false end
    toggle(sel)                                    -- the item moves; the selection stays put,
    if not selectable(sel) and not moveSel(-1) then moveSel(1) end   -- so the next item slides under it
    save()
  elseif k == keys.UP then return moveSel(-1)
  elseif k == keys.DOWN then return moveSel(1)
  elseif k == keys.ENTER then
    if isAddRow(sel) then startAdd(addPosition(sel)) else startEdit(sel) end
  elseif ch == "a" or ch == "A" or ch == "n" then
    startAdd(addPosition(sel))
  elseif fn == "up" or fn == "down" then                -- move the item
    local d = fn == "up" and -1 or 1
    local j = sel + d
    if not e or not e.item or j < 1 or j > #cur.entries then return false end
    local o = cur.entries[j]
    if not o.item or o.done ~= e.done then return false end   -- stay within the checked / unchecked group
    cur.entries[sel], cur.entries[j] = cur.entries[j], cur.entries[sel]
    sel = j
    save()
  elseif k == keys.BKSP then
    if not e or not e.item then return false end
    if was == "delete" then
      table.remove(cur.entries, sel)
      if sel > rowCount() then sel = rowCount() end
      if not selectable(sel) and not moveSel(-1) then moveSel(1) end
      save()
      message = "Deleted"
    else
      pending = "delete"
      message = "Bksp again to delete this item"
    end
  elseif ch == "c" or ch == "C" then
    local d = counts(cur.entries)
    if d == 0 then message = "Nothing checked yet"; return end
    if was == "clear" then
      local keep = {}
      for _, x in ipairs(cur.entries) do if not (x.item and x.done) then keep[#keep + 1] = x end end
      cur.entries = keep
      sel = math.min(sel, rowCount())
      if not selectable(sel) and not moveSel(-1) then moveSel(1) end
      save()
      message = "Cleared " .. d .. " checked item" .. (d == 1 and "" or "s")
    else
      pending = "clear"
      message = "c again to remove " .. d .. " checked item" .. (d == 1 and "" or "s")
    end
  elseif k == keys.TAB then mode = "help"; helpPage = 1; screen.redraw(true)
  else
    pending = was
    return false
  end
end

local function editKey(k, ch, fn)
  if X_FINISHES_EDIT and (ch == "x" or ch == "X") then finishEdit(); return end
  if k == keys.ENTER then
    local at = editIdx
    if finishEdit() then startAdd(addPosition(at)) end   -- next item; an empty one ends editing
  elseif k == keys.UP or k == keys.DOWN then
    finishEdit()
    moveSel(k == keys.UP and -1 or 1)
  elseif k == keys.LEFT then if editPos > 0 then editPos = editPos - 1 else return false end
  elseif k == keys.RIGHT then if editPos < #editText then editPos = editPos + 1 else return false end
  elseif k == keys.BKSP then
    if editPos == 0 then
      if editText == "" then finishEdit() end         -- Bksp on an empty item removes it
      return
    end
    editText = editText:sub(1, editPos - 1) .. editText:sub(editPos + 1)
    editPos = editPos - 1
  elseif k == keys.DEL or fn == "del" then
    if editPos >= #editText then return false end
    editText = editText:sub(1, editPos) .. editText:sub(editPos + 2)
  elseif fn == "v" then                              -- paste (first line of the clipboard)
    local t = (sys.clipboard() or ""):match("^[^\n]*"):gsub("\t", " ")
    editText = editText:sub(1, editPos) .. t .. editText:sub(editPos + 1)
    editPos = editPos + #t
  elseif fn == "c" then sys.clipboard(editText)
  elseif ch then
    editText = editText:sub(1, editPos) .. ch .. editText:sub(editPos + 1)
    editPos = editPos + 1
  else return false end
end

local function nameKey(k, ch)
  if k == keys.ENTER then
    local f = fileNameFor(nameText)
    if not f then return false end
    if f:lower() ~= (nameFor or ""):lower() and file.exists(LISTS .. "/" .. f) then nameWarn = titleOf(f); return end
    if nameFor then                                   -- rename
      if f ~= nameFor then file.rename(LISTS .. "/" .. nameFor, LISTS .. "/" .. f) end
      message = "Renamed to " .. titleOf(f)
      mode = "lists"
      scanLists()
      for i, l in ipairs(lists) do if l.file == f then lsel = i + 1 end end
    else                                              -- new list: straight into adding items
      file.write(LISTS .. "/" .. f, "")
      scanLists()
      openList(f)
      startAdd(1)
    end
  elseif k == keys.BKSP then nameText = nameText:sub(1, -2); nameWarn = nil
  elseif ch and ch:match("[%w%-_' ]") and #nameText < 30 then nameText = nameText .. ch; nameWarn = nil
  else return false end
end

function init()
  file.mkdir(LISTS)
  scanLists()
end

function key(k, ch, fn)
  if mode ~= "list" then message = nil end
  if mode == "lists" then message = nil; return listsKey(k, ch, fn) end
  if mode == "list" then message = nil; return listKey(k, ch, fn) end
  if mode == "edit" then return editKey(k, ch, fn) end
  if mode == "name" then return nameKey(k, ch) end
  if mode == "help" then
    local pages = math.ceil(#HELP / helpRows())
    if k == keys.TAB or k == keys.ENTER then mode = "list"; screen.redraw(true)
    elseif (k == keys.DOWN or k == keys.RIGHT) and pages > 1 then helpPage = helpPage % pages + 1
    elseif (k == keys.UP or k == keys.LEFT) and pages > 1 then helpPage = (helpPage - 2) % pages + 1
    else return false end
    return
  end
  if mode == "delete" then
    if k == keys.LEFT or k == keys.RIGHT or k == keys.UP or k == keys.DOWN then delYes = not delYes
    elseif ch == "y" then delYes = true
    elseif ch == "n" then delYes = false
    elseif k == keys.ENTER then
      if delYes then
        file.remove(LISTS .. "/" .. lists[lsel - 1].file)
        message = "Deleted " .. lists[lsel - 1].title
        scanLists()
      end
      mode = "lists"
    else return false end
  end
end

function back()
  if mode == "lists" then return false end            -- leave the app
  if mode == "edit" then finishEdit()
  elseif mode == "list" then closeList()
  elseif mode == "help" then mode = "list"; screen.redraw(true)
  elseif mode == "name" or mode == "delete" then mode = cur and "list" or "lists"
  end
  return true
end

-- ---------------------------------------------------------------------
--  Drawing
-- ---------------------------------------------------------------------
local function fit(s, n) return #s <= n and s or s:sub(1, math.max(1, n - 1)) .. "~" end

local function header(title)
  screen.text(M, screen.top + 2 * S, title)
  screen.rect(0, screen.top + screen.lh + S, screen.W, S, true)
  return screen.top + screen.lh + 3 * S
end

local function footer(hint) screen.text(M, screen.H - screen.lh + S, fit(hint, screen.cols)) end

local function checkbox(x, y, done)
  local sz = CW
  local by = y + ASC - sz + S
  screen.rect(x, by, sz, sz)
  if S > 1 then screen.rect(x + 1, by + 1, sz - 2, sz - 2) end
  if done then                                       -- an X, like marking a ballot
    for t = 0, S - 1 do
      screen.line(x + 2 + t, by + 2, x + sz - 3 + t, by + sz - 3)
      screen.line(x + 2 + t, by + sz - 3, x + sz - 3 + t, by + 2)
    end
  end
end

local function drawLists()
  local y0 = header("CHECKLISTS")
  local rows = (screen.H - screen.lh - y0) // screen.lh - (message and 1 or 0)
  local items = { "+ New checklist" }
  for _, l in ipairs(lists) do
    local c = l.unreadable and "?" or (l.done .. "/" .. l.total)
    local t = fit(l.title, screen.cols - #c - 3)
    items[#items + 1] = t .. string.rep(" ", screen.cols - 1 - #t - #c) .. c
  end
  if lsel < ltop then ltop = lsel end
  if lsel >= ltop + rows then ltop = lsel - rows + 1 end
  for r = 0, rows - 1 do
    local it = items[ltop + r]
    if not it then break end
    local y = y0 + r * screen.lh
    screen.text(M, y + S, it)
    if ltop + r == lsel then screen.invert(0, y, screen.W, screen.lh) end
  end
  if message then screen.text(M, screen.H - 2 * screen.lh + S, fit(message, screen.cols)) end
  footer("Enter=open  r=rename  Bksp=delete")
end

local function drawList()
  -- Progress line and bar
  local d, t = counts(cur.entries)
  local y = screen.top + 2 * S
  local label = t == 0 and "Empty list" or (d .. " of " .. t .. " done")
  screen.text(M, y, label)
  local bx = M + (#label + 2) * screen.cw
  local bw = screen.W - M - bx
  if t > 0 and bw > 20 * S then
    local bh = screen.ch - 2 * S
    screen.rect(bx, y + S, bw, bh)
    screen.rect(bx, y + S, math.floor(bw * d / t), bh, true)
  end
  y = y + screen.lh + S
  screen.rect(0, y, screen.W, S, true)
  y = y + 2 * S

  -- Rows
  local bottom = screen.H - screen.lh - (message and screen.lh or 0)
  local rows = math.max(1, (bottom - y) // ROW)
  if sel < top then top = sel end
  if sel >= top + rows then top = sel - rows + 1 end
  local textX = M + 2 * CW
  local cols = (screen.W - textX - M) // CW

  for r = 0, rows - 1 do
    local i = top + r
    if i > rowCount() then break end
    local ry = y + r * ROW
    local ty = ry + S
    local editing = mode == "edit" and i == editIdx

    if isAddRow(i) then
      screen.font(FONT)
      screen.text(M, ty, "+ Add item")
    else
      local e = cur.entries[i]
      if e.item then
        checkbox(M, ty, e.done)
        screen.font(FONT)
        if editing then
          -- scroll the text so the cursor stays visible
          local start = math.max(0, editPos - cols + 1)
          local shown = editText:sub(start + 1, start + cols)
          screen.text(textX, ty, shown)
          screen.rect(textX + (editPos - start) * CW, ty, math.max(2, 2 * S), LH, true)
          screen.rect(textX - S, ry, screen.W - textX - M + 2 * S, ROW - S)       -- edit box
        else
          local shown = fit(e.text, cols)
          screen.text(textX, ty, shown)
          if e.done then screen.rect(textX, ty + ASC - ASC * 2 // 5, #shown * CW, S, true) end   -- strike
        end
      else
        -- a line that isn't an item (e.g. "## Produce"): a section label
        screen.font(FONTB)
        local label = e.raw:gsub("^%s*#+%s*", "")
        screen.text(M, ty, fit(label, (screen.W - 2 * M) // CW))
        screen.font()
        screen.rect(M, ry + ROW - 2 * S, screen.W - 2 * M, S, true)
      end
    end
    screen.font()
    if i == sel and mode == "list" then screen.invert(0, ry, screen.W, ROW) end
  end

  if message then screen.text(M, screen.H - 2 * screen.lh + S, fit(message, screen.cols)) end
  if mode == "edit" then footer("Enter=next item  Esc=done")
  elseif #cur.entries == 0 then footer("Enter or a = add an item")
  else footer("x=check a=add Enter=edit Tab=help") end
end

local function drawHelp()
  local rows = helpRows()
  local pages = math.ceil(#HELP / rows)
  if helpPage > pages then helpPage = 1 end
  local y = header("CHECKLIST KEYS" .. (pages > 1 and ("  " .. helpPage .. "/" .. pages) or ""))
  for i = (helpPage - 1) * rows + 1, math.min(#HELP, helpPage * rows) do
    local h = HELP[i]
    if h[1] ~= "" then screen.font(FONTB); screen.text(M, y, h[1]) end
    screen.font(FONT)
    screen.text(M + 11 * CW, y, h[2])
    screen.font()
    y = y + LH
  end
  footer(pages > 1 and "Up/Down=more  Tab=back" or "Tab=back")
end

local function drawName()
  local y0 = header(nameFor and "Rename checklist" or "New checklist")
  screen.text(M, y0 + screen.lh // 2, "Name:")
  local boxY = y0 + screen.lh * 3 // 2 + 2 * S
  local boxH = screen.lh + 4 * S
  screen.rect(M, boxY, screen.W - 2 * M, boxH)
  local maxc = (screen.W - 4 * M) // screen.cw - 1
  local shown = #nameText > maxc and nameText:sub(-maxc) or nameText
  screen.text(2 * M, boxY + 3 * S, shown)
  screen.rect(2 * M + #shown * screen.cw, boxY + 3 * S, 5 * S, screen.ch, true)
  if nameWarn then screen.text(M, boxY + boxH + screen.lh // 2, fit(nameWarn .. " already exists.", screen.cols)) end
  footer("Enter=OK  Esc=cancel")
end

local function drawDelete()
  local y0 = header("Delete checklist?")
  screen.text(M, y0 + screen.lh, fit(lists[lsel - 1].title, screen.cols))
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
  if mode == "lists" then drawLists()
  elseif mode == "list" or mode == "edit" then drawList()
  elseif mode == "help" then drawHelp()
  elseif mode == "name" then drawName()
  elseif mode == "delete" then drawDelete()
  end
end
