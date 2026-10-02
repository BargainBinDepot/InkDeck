-- Calc: type an expression, Enter to evaluate.
-- Keys: 0-9 . + - * / % ^ ( )   Enter = evaluate   Bksp = delete   c = clear
-- The last answer can be used as "ans", e.g. ans*2

local expr = ""
local result = ""
local history = {}          -- { "2+2 = 4", ... } newest first
local ans = 0

local ALLOWED = "^[%d%.%+%-%*/%%%^%(%)%s]*$"

local function evaluate()
  if expr == "" then return end
  local src = expr:gsub("ans", "(" .. tostring(ans) .. ")")
  if not src:match(ALLOWED) then result = "?"; return end
  local f = load("return " .. src, "calc", "t", {})   -- empty environment: pure maths only
  local ok, v = pcall(f or error)
  if ok and type(v) == "number" then
    if v == math.floor(v) and math.abs(v) < 1e15 then v = math.floor(v) end
    ans = v
    result = tostring(v)
    table.insert(history, 1, expr .. " = " .. result)
    if #history > 20 then table.remove(history) end
    expr = ""
  else
    result = "Error"
  end
end

function key(k, ch)
  if k == keys.ENTER then evaluate()
  elseif k == keys.BKSP then expr = expr:sub(1, -2)
  elseif ch == "c" or ch == "C" then expr = ""; result = ""
  elseif ch == "a" then expr = expr .. "ans"
  elseif ch and ch:match("[%d%.%+%-%*/%%%^%(%) ]") then expr = expr .. ch
  else return false end      -- nothing changed: skip the redraw
end

function draw()
  local s, m, top = screen.scale, screen.margin, screen.top
  local big = 2 * s

  -- expression box
  local boxH = 8 * big + 8 * s
  screen.rect(m, top + 3 * s, screen.W - 2 * m, boxH)
  local shown = expr
  local maxChars = math.floor((screen.W - 4 * m) / (6 * big)) - 1
  if #shown > maxChars then shown = "<" .. shown:sub(-maxChars + 1) end
  screen.text(2 * m, top + 3 * s + 4 * s, shown .. "_", big)

  -- result, right aligned
  local ry = top + 3 * s + boxH + 4 * s
  if result ~= "" then
    local r = "= " .. result
    screen.text(screen.W - m - screen.textwidth(r, big), ry, r, big)
  end

  -- history
  local y = ry + 8 * big + 6 * s
  for _, line in ipairs(history) do
    if y > screen.H - 2 * screen.lh then break end
    screen.text(m, y, line)
    y = y + screen.lh
  end

  screen.text(m, screen.H - screen.lh, "Enter = calc  c = clear  a = ans")
end
