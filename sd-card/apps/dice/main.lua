-- =====================================================================
--  Dice — port of the ESP32-C3 D&D Dice Roller to InkDeck
--
--  Pick a die, choose how many, roll. Uses the ESP32's hardware random
--  number generator (sys.random) with unbiased sampling, like the original.
--
--  Dice:    Left/Right choose, Enter select ("Custom" = any number of sides)
--  Sides:   type a number, Left/Right -1/+1 (wrapping 1 <-> 999), Up/Down +10/-10, Enter OK
--  Count:   Left/Right -1/+1 (wrapping 1 <-> 99), Up/Down +10/-10, or type a number
--           + / -  modifier        a / d  advantage / disadvantage (1 D20)
--           Enter or Space = roll
--  Result:  Enter or Space = roll again, Left = dice, Right = count
-- =====================================================================

local DICE = {
  { name = "D4",   sides = 4,   icon = "icons/d4.bmp" },
  { name = "D6",   sides = 6,   icon = "icons/d6.bmp" },
  { name = "D8",   sides = 8,   icon = "icons/d8.bmp" },
  { name = "D10",  sides = 10,  icon = "icons/d10.bmp" },
  { name = "D12",  sides = 12,  icon = "icons/d12.bmp" },
  { name = "D20",  sides = 20,  icon = "icons/d20.bmp" },
  { name = "D100", sides = 100, icon = "icons/d10.bmp" },   -- a percentile die is shaped like a d10
  { custom = true, icon = "icons/custom.bmp" },            -- any number of sides you like
}
local MAX_COUNT, MAX_MOD = 99, 50
local MIN_SIDES, MAX_SIDES = 1, 999

local S, M = screen.scale, screen.margin
local ICON = 40 * S                    -- the original 40x40 icons, pixel-doubled on the 3.7"
local BIG = 3 * S                      -- big number text size
local WIDE = screen.W >= 400           -- room for a history column

local mode = "dice"                    -- dice | sides | count | result
local customSides = 2                  -- the Custom die (remembered in custom.txt)
local die = 6                          -- start on the D20
local count, mod = 1, 0
local adv = 0                          -- 0 normal, 1 advantage, -1 disadvantage
local typing = false                   -- digits typed so far build a number
local result = nil                     -- { total, values, kept, other, expr, natural }
local history = {}

-- The selected die. The Custom one takes its name and sides from customSides.
local function d()
  local dd = DICE[die]
  if dd.custom then return { name = "D" .. customSides, sides = customSides, icon = dd.icon, custom = true } end
  return dd
end
local function advAllowed() return d().sides == 20 and count == 1 end

local function expr()
  local s = (count > 1 and tostring(count) or "") .. d().name
  if adv == 1 then s = s .. " adv" elseif adv == -1 then s = s .. " dis" end
  if mod > 0 then s = s .. "+" .. mod elseif mod < 0 then s = s .. mod end
  return s
end

-- ---------------------------------------------------------------------
--  Rolling
-- ---------------------------------------------------------------------
local function rollOne(sides, percentile)
  if percentile then                   -- D100: tens die (00-90) + ones die (0-9)
    local tens, ones = sys.random(0, 9) * 10, sys.random(0, 9)
    if tens == 0 and ones == 0 then return 100 end
    return tens + ones
  end
  return sys.random(1, sides)
end

local function roll()
  if count < 1 then count = 1 end
  local r = { expr = expr() }
  if adv ~= 0 and advAllowed() then
    local a, b = rollOne(20), rollOne(20)
    r.kept = adv == 1 and math.max(a, b) or math.min(a, b)
    r.other = (r.kept == a) and b or a
    r.values = { r.kept }
    r.total = r.kept + mod
  else
    r.values = {}
    local sum = 0
    for i = 1, count do
      local v = rollOne(d().sides, d().name == "D100" and not d().custom)
      r.values[i] = v
      sum = sum + v
    end
    r.total = sum + mod
  end
  -- A natural 20 or 1 only counts on a single d20
  if d().sides == 20 and count == 1 then
    if r.values[1] == 20 then r.natural = 20 elseif r.values[1] == 1 then r.natural = 1 end
  end
  result = r
  table.insert(history, 1, r)
  if #history > 20 then table.remove(history) end
  mode = "result"
  screen.redraw(true)
end

function init()
  local saved = tonumber(file.read("custom.txt") or "")
  if saved and saved >= MIN_SIDES and saved <= MAX_SIDES then customSides = saved end
end

-- ---------------------------------------------------------------------
--  Keys
-- ---------------------------------------------------------------------
function key(k, ch)
  if mode == "dice" then
    if k == keys.LEFT or k == keys.UP then die = (die - 2) % #DICE + 1
    elseif k == keys.RIGHT or k == keys.DOWN then die = die % #DICE + 1
    elseif k == keys.ENTER or ch == " " then
      count, mod, adv, typing = 1, 0, 0, false        -- like the original: start at 1
      mode = DICE[die].custom and "sides" or "count"
    else return false end
    return
  end

  if mode == "sides" then                             -- how many sides for the Custom die
    if ch and ch:match("%d") then
      local n = tonumber(ch)
      if typing and customSides * 10 + n <= MAX_SIDES then customSides = customSides * 10 + n else customSides = n end
      typing = true
    else
      typing = false
      if k == keys.LEFT then customSides = customSides <= MIN_SIDES and MAX_SIDES or customSides - 1
      elseif k == keys.RIGHT then customSides = customSides >= MAX_SIDES and MIN_SIDES or customSides + 1
      elseif k == keys.UP then customSides = math.min(MAX_SIDES, customSides + 10)
      elseif k == keys.DOWN then customSides = math.max(MIN_SIDES, customSides - 10)
      elseif k == keys.BKSP then customSides = customSides // 10
      elseif k == keys.ENTER or ch == " " then
        if customSides < MIN_SIDES then customSides = MIN_SIDES end
        file.write("custom.txt", tostring(customSides))
        mode = "count"
        return
      else return false end
    end
    return
  end

  if mode == "count" then
    if ch and ch:match("%d") then
      local n = tonumber(ch)
      if typing and count * 10 + n <= MAX_COUNT then count = count * 10 + n else count = n end
      typing = true
    else
      typing = false
      if k == keys.LEFT then count = count <= 1 and MAX_COUNT or count - 1
      elseif k == keys.RIGHT then count = count >= MAX_COUNT and 1 or count + 1
      elseif k == keys.UP then count = math.min(MAX_COUNT, count + 10)
      elseif k == keys.DOWN then count = math.max(1, count - 10)
      elseif ch == "+" or ch == "=" then mod = math.min(MAX_MOD, mod + 1)
      elseif ch == "-" or ch == "_" then mod = math.max(-MAX_MOD, mod - 1)
      elseif ch == "a" or ch == "A" then if advAllowed() then adv = adv == 1 and 0 or 1 else return false end
      elseif ch == "d" or ch == "D" then if advAllowed() then adv = adv == -1 and 0 or -1 else return false end
      elseif k == keys.ENTER or ch == " " then roll(); return
      elseif k == keys.BKSP then count = math.max(1, count // 10)
      else return false end
    end
    if not advAllowed() then adv = 0 end
    return
  end

  if mode == "result" then
    if k == keys.ENTER or ch == " " or ch == "r" then roll()
    elseif k == keys.LEFT then mode = "dice"; screen.redraw(true)
    elseif k == keys.RIGHT then mode = "count"; typing = false; screen.redraw(true)
    else return false end
  end
end

function back()
  if mode == "dice" then return false end             -- leave the app
  if mode == "sides" then mode = "dice"
  elseif mode == "count" then mode = DICE[die].custom and "sides" or "dice"; typing = false
  else mode = "count"; typing = false end
  screen.redraw(true)
  return true
end

-- ---------------------------------------------------------------------
--  Drawing
-- ---------------------------------------------------------------------
local function footer(t)
  local s = #t > screen.cols and t:sub(1, screen.cols) or t
  screen.text(M, screen.H - screen.lh + S, s)
end

local function bigCentered(cx, y, text, size)
  local w = screen.textwidth(text, size)
  screen.text(cx - w // 2, y, text, size)
  return w
end

local function drawDice()
  local top = screen.top
  local midY = top + (screen.H - top - 2 * screen.lh) // 2
  -- icon on the left of centre, name on the right, arrows at the edges
  local iconX = screen.W // 2 - ICON - 4 * S
  screen.image(d().icon, iconX, midY - ICON // 2, ICON)
  local custom = DICE[die].custom
  screen.text(screen.W // 2 + 4 * S, midY - 4 * BIG, custom and "D?" or d().name, BIG)
  screen.text(screen.W // 2 + 4 * S, midY + 4 * BIG + 3 * S, custom and "any sides" or (d().sides .. " sides"), S)
  screen.text(M, midY - 8 * S, "<", 2 * S)
  screen.text(screen.W - M - 12 * S, midY - 8 * S, ">", 2 * S)

  -- strip of all the dice, the current one highlighted
  local y = screen.H - 2 * screen.lh - 2 * S
  local x = M
  local total = 0
  local names = {}
  for i, dd in ipairs(DICE) do names[i] = dd.custom and "D?" or dd.name end
  for _, n in ipairs(names) do total = total + #n + 1 end
  x = (screen.W - (total - 1) * screen.cw) // 2
  for i, n in ipairs(names) do
    screen.text(x, y, n)
    if i == die then screen.invert(x - S, y - S, #n * screen.cw + 2 * S, screen.ch + 2 * S) end
    x = x + (#n + 1) * screen.cw
  end
  footer(DICE[die].custom and "Enter to choose the sides" or "Left/Right choose  Enter select")
end

local function drawSides()
  local top = screen.top + 3 * S
  screen.text(M, top, "How many sides?")
  local cx = screen.W // 2
  local y = top + screen.lh + 4 * S
  bigCentered(cx, y, tostring(customSides), BIG + S)
  screen.text(M, y + 4 * S, "<", 2 * S)
  screen.text(screen.W - M - 12 * S, y + 4 * S, ">", 2 * S)
  local y2 = y + 8 * (BIG + S) + 4 * S
  bigCentered(cx, y2, "D" .. customSides, 2 * S)
  bigCentered(cx, y2 + 16 * S + 2 * S, "From " .. MIN_SIDES .. " to " .. MAX_SIDES .. " sides", S)
  footer("Type a number  Enter OK")
end

local function drawCount()
  local top = screen.top + 3 * S
  screen.text(M, top, "How many " .. d().name .. "?")
  local cx = screen.W // 2
  local y = top + screen.lh + 4 * S
  bigCentered(cx, y, tostring(count), BIG + S)
  screen.text(M, y + 4 * S, "<", 2 * S)
  screen.text(screen.W - M - 12 * S, y + 4 * S, ">", 2 * S)

  local y2 = y + 8 * (BIG + S) + 4 * S
  bigCentered(cx, y2, expr(), 2 * S)
  if advAllowed() then
    local label = adv == 1 and "Advantage: keep the higher"
               or adv == -1 and "Disadvantage: keep the lower"
               or "a=advantage  d=disadvantage"
    bigCentered(cx, y2 + 16 * S + 2 * S, label, S)
  end
  footer("Enter roll  +/- mod  0-9 count")
end

local function drawResult()
  local r = result
  local colW = WIDE and screen.W * 2 // 3 or screen.W
  local cx = colW // 2
  local y = screen.top + 3 * S

  -- natural 20 / natural 1 banner
  if r.natural == 20 then                             -- white on black banner
    bigCentered(cx, y + 2 * S, "NATURAL 20!", S)
    screen.invert(M, y, colW - 2 * M, screen.lh + 2 * S)
    y = y + screen.lh + 5 * S
  elseif r.natural == 1 then
    local w = screen.textwidth("Natural 1...")
    screen.rect(M, y, colW - 2 * M, screen.lh + 2 * S)
    screen.text(cx - w // 2, y + 2 * S, "Natural 1...")
    y = y + screen.lh + 5 * S
  else
    bigCentered(cx, y, r.expr .. " =", S)
    y = y + screen.lh + 2 * S
  end

  -- the total, big
  bigCentered(cx, y, tostring(r.total), BIG + S)
  y = y + 8 * (BIG + S) + 4 * S

  -- the individual dice
  local detail
  if r.kept then
    detail = "Rolled " .. r.kept .. " and " .. r.other .. ", kept " .. r.kept
  elseif #r.values > 1 or mod ~= 0 then
    detail = table.concat(r.values, " + ")
  end
  if detail and mod ~= 0 and #r.values >= 1 then
    detail = detail .. (mod > 0 and ("  (+" .. mod .. ")") or ("  (" .. mod .. ")"))
  end
  if detail then
    local cols = (colW - 2 * M) // screen.cw
    local lines = screen.wrap(detail, cols)
    local room = (screen.H - screen.lh - y) // screen.lh
    for i = 1, math.min(#lines, room) do
      local line = lines[i]
      if i == room and #lines > room then line = line:sub(1, cols - 4) .. " ..." end
      bigCentered(cx, y, line, S)
      y = y + screen.lh
    end
  end

  -- history column (3.7")
  if WIDE then
    local hx = colW + M
    screen.rect(colW, screen.top + 2 * S, S, screen.H - screen.top - screen.lh - 4 * S, true)
    local hy = screen.top + 3 * S
    screen.font("mono9b"); screen.text(hx, hy, "History"); screen.font()
    hy = hy + screen.lh + S
    local hcols = (screen.W - hx - M) // screen.cw
    for i = 1, #history do
      if hy > screen.H - 2 * screen.lh then break end
      local h = history[i]
      local line = h.expr .. " " .. h.total
      if #line > hcols then line = h.expr:sub(1, hcols - #tostring(h.total) - 2) .. "~ " .. h.total end
      screen.text(hx, hy, line)
      if i == 1 then screen.invert(hx - S, hy - S, screen.W - hx, screen.ch + 2 * S) end
      hy = hy + screen.lh
    end
  end
  footer("Enter again  < dice  > count")
end

function draw()
  if mode == "dice" then drawDice()
  elseif mode == "sides" then drawSides()
  elseif mode == "count" then drawCount()
  else drawResult() end
end
