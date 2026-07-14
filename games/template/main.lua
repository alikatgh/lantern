-- lantern starter template — copy this folder, rename it, make a game.
-- Run:  ./build/lantern games/yourgame
--
-- The screen is ALWAYS 400x240 (lt.W x lt.H), integer-upscaled to the
-- window. 2D coordinates are pixels, y-down. 3D is right-handed, y-up.
-- Full API: docs/ENGINE.md · C version of everything: include/lantern.h

local cube = lt.cube()
local x, y = lt.W / 2, lt.H / 2   -- player position (2D, pixels)
local best = tonumber(lt.load_save("template_best") or "") or 0
local t = 0

-- update runs first each frame (dt = seconds, <= 0.1)
function update(dt)
  t = t + dt
  local speed = 120
  if lt.key("left")  then x = x - speed * dt end
  if lt.key("right") then x = x + speed * dt end
  if lt.key("up")    then y = y - speed * dt end
  if lt.key("down")  then y = y + speed * dt end
  if lt.pressed("z") then                    -- edge, not hold
    best = best + 1
    lt.save("template_best", tostring(best))
  end
end

-- draw runs after update; 3D first, then 2D composites on top
function draw()
  lt.clear(0.12, 0.10, 0.20)

  -- a 3D backdrop: slow-spinning cube under a warm point light
  lt.camera(0, 1.5, 4, 0, 0, 0, 55)
  lt.light(0.4, -1, -0.3, 0.35)
  lt.point_light(0, 1.5, 1, 1.5, 4, 1.0, 0.7, 0.3)
  lt.draw(cube, 0, 0, 0, t * 0.4, t * 0.6, 0, 1, 1, 1, 0.6, 0.7, 0.9)

  -- your 2D game on top
  lt.rect(x - 4, y - 4, 8, 8, 1, 0.85, 0.3)
  lt.print("ARROWS: MOVE  Z: COUNT (" .. best .. ")", 4, lt.H - 11,
           1, 1, 1, 0.8)
end
