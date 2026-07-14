-- KORA NIGHT — the lantern showcase game.
-- Keep the stupa's four butter lamps lit through a windy night. Wind blows
-- them out; walk to a dark lamp and press Z to relight it. Score a point per
-- relight; 60 seconds; hi-score persists.
--
-- Why this game exists: it exercises every engine subsystem AS A GAME —
-- the 4 point lights ARE the mechanic, billboards are the player, fog is
-- the night, audio+rumble are feedback, saves keep the best score.
--
-- SHOWCASE_AUTO=1: deterministic self-playing round (short timer, monk
-- walks to unlit lamps and relights them) for LANTERN_SHOT verification.

local AUTO = os.getenv("SHOWCASE_AUTO") ~= nil
if AUTO then math.randomseed(7) end

local ground    = lt.plane(12)
local drum      = lt.cylinder(20)
local dome      = lt.sphere(14)
local spire     = lt.cone(12)
local cube      = lt.cube()
local mountains = lt.load_mesh("mountains.obj")
local monk      = lt.load_texture("monk.bmp")
local bell      = lt.load_sound("bell.wav")
local whoosh    = lt.load_sound("whoosh.wav")

local LAMPS = {                      -- one per point-light slot (0..3)
  { x =  3.4, z =  3.4 }, { x = -3.4, z =  3.4 },
  { x = -3.4, z = -3.4 }, { x =  3.4, z = -3.4 },
}
local ROUND = AUTO and 8 or 60

local state, t = "title", 0          -- title | play | over
local px, pz = 0, 5.2                -- player
local score, timer = 0, ROUND
local wind_in = 3.0                  -- seconds until next gust
local best = tonumber(lt.load_save("koranight_best") or "") or 0

local function reset_lamps()
  for _, l in ipairs(LAMPS) do l.lit = true end
end

local function start_round()
  state, t = "play", 0
  score, timer, wind_in = 0, ROUND, 3.0
  px, pz = 0, 5.2
  reset_lamps()
end
reset_lamps()

local function nearest_unlit()
  local bi, bd
  for i, l in ipairs(LAMPS) do
    if not l.lit then
      local d = (l.x - px) ^ 2 + (l.z - pz) ^ 2
      if not bd or d < bd then bi, bd = i, d end
    end
  end
  return bi, bd
end

local function relight(i)
  LAMPS[i].lit = true
  score = score + 1
  lt.play(bell, 0.8, false)
  lt.rumble(0.3, 0.6, 120)
end

function update(dt)
  t = t + dt
  if state == "title" then
    if lt.pressed("z") or lt.pressed("return") or (AUTO and t > 0.5) then
      start_round()
    end
    return
  end
  if state == "over" then
    if lt.pressed("z") or lt.pressed("return") then state = "title" end
    return
  end

  -- play
  timer = timer - dt
  if timer <= 0 then
    timer = 0
    state = "over"
    if score > best then
      best = score
      lt.save("koranight_best", tostring(best))
    end
    return
  end

  -- wind: gusts come faster as the round goes on
  wind_in = wind_in - dt
  if wind_in <= 0 then
    wind_in = 1.2 + math.random() * (2.6 * (timer / ROUND) + 0.4)
    local lit = {}
    for i, l in ipairs(LAMPS) do if l.lit then lit[#lit + 1] = i end end
    if #lit > 0 then
      LAMPS[lit[math.random(#lit)]].lit = false
      lt.play(whoosh, 0.7, false)
      lt.rumble(0.5, 0.2, 200)
    end
  end

  -- movement (or autopilot toward the nearest dark lamp)
  local dx, dz = 0, 0
  if AUTO then
    local i = nearest_unlit()
    if i then
      dx = LAMPS[i].x - px
      dz = LAMPS[i].z - pz
      local len = math.sqrt(dx * dx + dz * dz)
      if len > 0.01 then dx, dz = dx / len, dz / len end
    end
  else
    if lt.key("left")  then dx = dx - 1 end
    if lt.key("right") then dx = dx + 1 end
    if lt.key("up")    then dz = dz - 1 end
    if lt.key("down")  then dz = dz + 1 end
  end
  px = math.max(-6.5, math.min(6.5, px + dx * 4.2 * dt))
  pz = math.max(-6.5, math.min(6.5, pz + dz * 4.2 * dt))
  -- keep out of the stupa
  local d = math.sqrt(px * px + pz * pz)
  if d < 2.2 and d > 0.01 then px, pz = px / d * 2.2, pz / d * 2.2 end

  -- relight when close
  local i, dist2 = nearest_unlit()
  if i and dist2 and dist2 < 1.1 then
    if AUTO or lt.pressed("z") then relight(i) end
  end
end

local function draw_world()
  lt.clear(0.10, 0.09, 0.20)
  lt.fog(7, 24, 0.10, 0.09, 0.20)
  lt.light(0.4, -1.0, -0.3, 0.22)

  for i, l in ipairs(LAMPS) do
    if l.lit then
      local fl = 3.6 + 0.4 * math.sin(t * 9 + i * 2.1)
      lt.point_light(i - 1, l.x, 0.8, l.z, fl, 1.0, 0.70, 0.28)
    else
      lt.point_light(i - 1, 0, 0, 0, 0, 0, 0, 0)
    end
    -- lamp body + flame cube when lit
    lt.draw(drum, l.x, 0.25, l.z, 0, 0, 0, 0.4, 0.5, 0.4, 0.5, 0.42, 0.3)
    if l.lit then
      lt.draw(cube, l.x, 0.62, l.z, 0, t * 3, 0, .14, .2, .14, 1.0, 0.8, 0.3)
    end
  end

  local ce = 8.5
  lt.camera(px * 0.35, 5.2, pz * 0.35 + ce, px * 0.6, 0.4, pz * 0.6, 52)

  lt.draw(mountains, 0, -0.15, 0, 0, 0, 0, 60, 10, 60, 0.30, 0.28, 0.42)
  lt.draw(ground, 0, 0, 0, 0, 0, 0, 15, 1, 15, 0.42, 0.40, 0.40)

  lt.draw(drum, 0, 0.30, 0, 0, 0, 0, 3.0, 0.6, 3.0, 0.88, 0.86, 0.84)
  lt.draw(dome, 0, 1.30, 0, 0, 0, 0, 2.2, 1.8, 2.2, 0.94, 0.93, 0.90)
  lt.draw(spire, 0, 2.75, 0, 0, 0, 0, 0.8, 1.2, 0.8, 0.98, 0.80, 0.25)

  lt.shadow(px, 0.02, pz, 0.42, 0.4)   -- grounding blob under the monk
  lt.billboard(monk, px, 0.75, pz, 1.0, 1.5)
end

function draw()
  draw_world()

  if state == "title" then
    lt.rect(0, 78, lt.W, 46, 0.03, 0.03, 0.06, 0.75)
    lt.print("KORA NIGHT", lt.W / 2 - 40, 88, 0.98, 0.80, 0.25, 1)
    lt.print("KEEP THE BUTTER LAMPS LIT", lt.W / 2 - 100, 102, 1, 1, 1, 0.9)
    if math.floor(t * 2) % 2 == 0 then
      lt.print("PRESS Z", lt.W / 2 - 28, 112, 1, 1, 1, 0.8)
    end
    lt.print("BEST: " .. best, 4, lt.H - 11, 0.7, 0.85, 0.7, 0.9)
  elseif state == "play" then
    lt.rect(0, 0, lt.W, 13, 0.03, 0.03, 0.06, 0.6)
    lt.print("LAMPS RELIT: " .. score, 4, 3, 1, 1, 1, 0.95)
    lt.print(string.format("TIME %d", math.ceil(timer)),
             lt.W - 8 * 8 - 4, 3, 0.98, 0.80, 0.25, 0.95)
    local i, dist2 = nearest_unlit()
    if i and dist2 and dist2 < 1.1 then
      lt.print("Z: RELIGHT", lt.W / 2 - 40, lt.H - 24, 1, 1, 0.6, 1)
    end
  else -- over
    lt.rect(0, 74, lt.W, 60, 0.03, 0.03, 0.06, 0.8)
    lt.print("TIME UP", lt.W / 2 - 28, 84, 0.98, 0.80, 0.25, 1)
    lt.print("LAMPS RELIT: " .. score, lt.W / 2 - 60, 98, 1, 1, 1, 1)
    lt.print(score >= best and best > 0 and "NEW BEST!" or
             ("BEST: " .. best), lt.W / 2 - 36, 108, 0.7, 0.85, 0.7, 1)
    lt.print("Z: AGAIN", lt.W / 2 - 32, 120, 1, 1, 1, 0.8)
  end
end
