-- lantern demo: monastery at dusk.
-- Exercises every v0.3 API path: fog, point lights (butter lamps), OBJ
-- loading (mountains), primitives (cylinder/sphere/cone stupa), billboards
-- (monk sprite in the 3D scene), custom mesh + texture (prayer flags),
-- sprite_ex (rotated watch), sound (bell on Z / pad A), pressed-edge input.
-- Arrows/stick orbit · Z rings the bell · Esc quits.

local cube      = lt.cube()
local ground    = lt.plane(12) -- finer grid → rounder per-vertex light pools
local drum      = lt.cylinder(20)
local dome      = lt.sphere(14)
local spire     = lt.cone(12)
local mountains = lt.load_mesh("mountains.obj")
local flags     = lt.load_texture("flags.bmp")
local monk      = lt.load_texture("monk.bmp")
local watch     = lt.load_texture("watch.bmp")
local bell      = lt.load_sound("bell.wav")

-- prayer-flag banner: custom mesh, 12 floats/vert (pos nrm uv rgba),
-- vertex color fades the far end into the dusk.
local banner = lt.mesh{
  -0.5,-0.5,0, 0,0,1,  0,1, 1,1,1,1,   0.5,-0.5,0, 0,0,1,  1,1, .6,.6,.7,1,
   0.5, 0.5,0, 0,0,1,  1,0, .6,.6,.7,1,-0.5,-0.5,0, 0,0,1,  0,1, 1,1,1,1,
   0.5, 0.5,0, 0,0,1,  1,0, .6,.6,.7,1,-0.5, 0.5,0, 0,0,1,  0,0, 1,1,1,1,
   0.5,-0.5,0, 0,0,-1, 1,1, .6,.6,.7,1,-0.5,-0.5,0, 0,0,-1, 0,1, 1,1,1,1,
  -0.5, 0.5,0, 0,0,-1, 0,0, 1,1,1,1,    0.5,-0.5,0, 0,0,-1, 1,1, .6,.6,.7,1,
  -0.5, 0.5,0, 0,0,-1, 0,0, 1,1,1,1,    0.5, 0.5,0, 0,0,-1, 1,0, .6,.6,.7,1,
}

local angle, spin, rung = 0.55, 0, 0

function update(dt)
  spin = spin + dt * 1.2
  if lt.key("left")  then angle = angle - dt * 1.5 end
  if lt.key("right") then angle = angle + dt * 1.5 end
  if lt.pressed("z") then lt.play(bell, 0.9, false); rung = rung + 1 end
end

function draw()
  -- dusk: fog and sky share one color so geometry melts into the horizon
  lt.clear(0.26, 0.22, 0.38)
  lt.fog(7, 26, 0.26, 0.22, 0.38)
  lt.light(0.5, -1.0, -0.4, 0.30)

  -- two flickering butter lamps (point lights + small gold flames)
  local fl = 3.4 + 0.5 * math.sin(lt.time() * 11) * math.sin(lt.time() * 6.7)
  lt.point_light(0, 2.2, 0.7, 2.2, fl, 1.0, 0.72, 0.30)
  lt.point_light(1, -2.2, 0.7, 2.2, fl, 1.0, 0.72, 0.30)
  lt.draw(cube, 2.2, 0.35, 2.2, 0, 0, 0, .22,.3,.22, 1.0, 0.8, 0.3)
  lt.draw(cube, -2.2, 0.35, 2.2, 0, 0, 0, .22,.3,.22, 1.0, 0.8, 0.3)

  local cx, cz = math.sin(angle) * 9, math.cos(angle) * 9
  lt.camera(cx, 4.2, cz, 0, 1.0, 0, 50)

  -- mountains (OBJ, flat-shaded) ring the courtyard, sunk into the fog;
  -- base sits below the ground plane so the flats never z-fight it
  lt.draw(mountains, 0, -0.15, 0, 0, 0, 0, 60, 10, 60, 0.42, 0.40, 0.52)
  -- courtyard ground
  lt.draw(ground, 0, 0, 0, 0, 0, 0, 15, 1, 15, 0.52, 0.48, 0.44)

  -- stupa from primitives: drum tiers, whitewashed dome, gold cone spire
  lt.draw(drum, 0, 0.30, 0, 0, 0, 0, 3.4, 0.6, 3.4, 0.90, 0.88, 0.86)
  lt.draw(drum, 0, 0.85, 0, 0, 0, 0, 2.6, 0.5, 2.6, 0.93, 0.91, 0.89)
  lt.draw(dome, 0, 1.65, 0, 0, 0, 0, 2.4, 2.0, 2.4, 0.96, 0.95, 0.92)
  lt.draw(drum, 0, 2.75, 0, 0, spin * 0.3, 0, 0.7, 0.5, 0.7, 0.98, 0.80, 0.25)
  lt.draw(spire, 0, 3.55, 0, 0, 0, 0, 0.9, 1.3, 0.9, 0.98, 0.80, 0.25)

  -- prayer-flag lines to two corner poles (textured custom mesh)
  for _, side in ipairs({ -1, 1 }) do
    lt.draw(cube, side * 6, 1.6, side * 6, 0, 0, 0, 0.18, 3.2, 0.18,
            0.45, 0.30, 0.18)
    local wave = math.sin(lt.time() * 2.0 + side) * 0.08
    lt.draw(banner, side * 3, 3.2 + wave, side * 3,
            0.25 * side, side * math.pi / 4 + math.pi / 2, 0,
            8.5, 0.55, 1, 1, 1, 1, flags)
  end

  -- the monk walks his kora around the stupa (billboard sprite in 3D,
  -- grounded by an ALBW-style blob shadow)
  local ka = lt.time() * 0.25
  local mx, mz = math.sin(ka) * 4.6, math.cos(ka) * 4.6
  lt.shadow(mx, 0.02, mz, 0.42, 0.4)
  lt.billboard(monk, mx, 0.75, mz, 1.0, 1.5)

  -- four prayer wheels, spinning
  for i = 0, 3 do
    local a = i * math.pi / 2 + math.pi / 4
    local x, z = math.sin(a) * 5.5, math.cos(a) * 5.5
    lt.draw(cube, x, 0.35, z, 0, 0, 0, 0.3, 0.7, 0.3, 0.45, 0.30, 0.18)
    lt.draw(drum, x, 0.95, z, 0, spin + i, 0, 0.5, 0.5, 0.5, 0.75, 0.15, 0.12)
  end

  -- 2D HUD: memory line, rotating watch (sprite_ex), status
  lt.rect(0, 0, lt.W, 14, 0.05, 0.05, 0.08, 0.55)
  lt.sprite_ex(watch, 9, 7, 1, 1, lt.time() * 0.8)
  lt.print("SHE WOUND THE WATCH. TICK.", 18, 3, 1, 1, 1, 0.9)
  lt.print("LANTERN V0.3", lt.W - 8 * 12 - 4, 3, 0.98, 0.80, 0.25, 0.9)
  lt.rect(0, lt.H - 14, lt.W, 14, 0.05, 0.05, 0.08, 0.55)
  lt.print((lt.gamepad() and "PAD OK" or "PAD: NONE") ..
           "  Z: BELL (" .. rung .. ")", 4, lt.H - 11, 0.7, 0.85, 0.7, 0.9)
  lt.print("ARROWS: ORBIT", lt.W - 8 * 13 - 4, lt.H - 11, 1, 1, 1, 0.7)
end
