-- draw-call stress (run with LANTERN_NOVSYNC=1): after a 60-frame warmup,
-- ramp mesh draws until avg frame time exceeds 16.6ms; report last count
-- that fit the 60fps budget.
local cube = lt.cube()
local n, t, frames, acc, warm = 500, 0, 0, 0, 0
local lastGood, settled = 0, ""
function update(dt)
  t = t + dt
  warm = warm + 1
  if warm < 60 then return end
  frames = frames + 1; acc = acc + dt
  if frames == 30 then
    local avg = acc / 30
    if settled == "" then
      if avg < 0.0166 then
        lastGood = n; n = math.floor(n * 1.4)
      else
        settled = string.format("BUDGET: %d DRAWS OK, %d = %.1f MS",
                                lastGood, n, avg * 1000)
        print(settled)
      end
    end
    frames, acc = 0, 0
  end
end
function draw()
  lt.clear(0.05, 0.05, 0.1)
  lt.camera(0, 26, 44, 0, 0, 0, 55)
  lt.light(0.4, -1, -0.3, 0.35)
  local side = math.ceil(n ^ (1/3))
  local i = 0
  for x = 0, side - 1 do for y = 0, side - 1 do for z = 0, side - 1 do
    if i >= n then break end
    lt.draw(cube, (x - side/2) * 1.4, (y - side/2) * 1.4, (z - side/2) * 1.4,
            t + i, t * 0.7, 0, 0.5, 0.5, 0.5,
            x / side, y / side, z / side)
    i = i + 1
  end end end
  lt.print("DRAWS: " .. n, 4, 4, 1, 1, 1, 1)
  if settled ~= "" then lt.print(settled, 4, 14, 1, 1, 0.5, 1) end
end
