-- TicTac: you are X, the device is O.
-- Arrows move, Enter places, n = new game. Score is saved between sessions.

local board, cursor, over, msg
local score = { you = 0, pda = 0, draw = 0 }

local LINES = { {1,2,3},{4,5,6},{7,8,9},{1,4,7},{2,5,8},{3,6,9},{1,5,9},{3,5,7} }

local function winner(b)
  for _, l in ipairs(LINES) do
    local a = b[l[1]]
    if a ~= " " and a == b[l[2]] and a == b[l[3]] then return a end
  end
  for i = 1, 9 do if b[i] == " " then return nil end end
  return "draw"
end

local function saveScore()
  file.write("score.txt", score.you .. "," .. score.pda .. "," .. score.draw)
end

local function loadScore()
  local s = file.read("score.txt")
  if s then
    local a, b, c = s:match("(%d+),(%d+),(%d+)")
    if a then score = { you = tonumber(a), pda = tonumber(b), draw = tonumber(c) } end
  end
end

local function newGame()
  board = { " ", " ", " ", " ", " ", " ", " ", " ", " " }
  cursor, over, msg = 5, false, "Your move"
end

-- Device move: win if it can, block if it must, else centre, corner, anything
local function pdaMove()
  for _, mark in ipairs({ "O", "X" }) do
    for i = 1, 9 do
      if board[i] == " " then
        board[i] = mark
        local w = winner(board)
        board[i] = " "
        if w == mark then board[i] = "O"; return end
      end
    end
  end
  for _, i in ipairs({ 5, 1, 3, 7, 9, 2, 4, 6, 8 }) do
    if board[i] == " " then board[i] = "O"; return end
  end
end

local function finish(w)
  over = true
  if w == "X" then score.you = score.you + 1; msg = "You win!  n = new game"
  elseif w == "O" then score.pda = score.pda + 1; msg = "I win!  n = new game"
  else score.draw = score.draw + 1; msg = "Draw.  n = new game" end
  saveScore()
end

function init()
  loadScore()
  newGame()
end

function key(k, ch)
  if ch == "n" then newGame(); return end
  if over then return false end
  if k == keys.LEFT and (cursor - 1) % 3 > 0 then cursor = cursor - 1
  elseif k == keys.RIGHT and (cursor - 1) % 3 < 2 then cursor = cursor + 1
  elseif k == keys.UP and cursor > 3 then cursor = cursor - 3
  elseif k == keys.DOWN and cursor < 7 then cursor = cursor + 3
  elseif k == keys.ENTER then
    if board[cursor] ~= " " then return false end
    board[cursor] = "X"
    local w = winner(board)
    if w then finish(w); return end
    pdaMove()
    w = winner(board)
    if w then finish(w) else msg = "Your move" end
  else
    return false
  end
end

function draw()
  local s, top = screen.scale, screen.top
  local size = screen.H - top - 6 * s            -- square board filling the height
  local cell = math.floor(size / 3)
  local x0 = screen.margin * 2
  local y0 = top + 3 * s

  for i = 1, 2 do
    screen.rect(x0 + i * cell - s, y0, 2 * s, cell * 3, true)
    screen.rect(x0, y0 + i * cell - s, cell * 3, 2 * s, true)
  end

  local glyph = math.max(1, math.floor(cell / 12))
  for i = 1, 9 do
    local cx = x0 + ((i - 1) % 3) * cell
    local cy = y0 + math.floor((i - 1) / 3) * cell
    if board[i] ~= " " then
      local w = screen.textwidth(board[i], glyph)
      screen.text(cx + (cell - w) / 2, cy + (cell - 8 * glyph) / 2, board[i], glyph)
    end
    if i == cursor and not over then screen.invert(cx + 3 * s, cy + 3 * s, cell - 6 * s, cell - 6 * s) end
  end

  -- side panel
  local px = x0 + cell * 3 + screen.margin * 2
  local y = y0
  screen.text(px, y, msg); y = y + 2 * screen.lh
  screen.text(px, y, "You: " .. score.you); y = y + screen.lh
  screen.text(px, y, "PDA: " .. score.pda); y = y + screen.lh
  screen.text(px, y, "Draw: " .. score.draw)
end
