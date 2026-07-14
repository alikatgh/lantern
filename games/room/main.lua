-- THE SHOP — ALBW-style interior demo (the target the user set with a
-- Link Between Worlds screenshot). One warm wooden room, near-overhead
-- camera, textured floor/walls/carpet, red furniture, a keyframe-animated
-- character (lt.draw_lerp) with a grounding shadow, a purple shopkeeper,
-- and a hearts + item-panel HUD. Everything rendered by our own rasterizer.
--
-- Arrows/stick walk. Esc quits.

local floorT  = lt.load_texture("floor.bmp")
local wallT   = lt.load_texture("wall.bmp")
local carpetT = lt.load_texture("carpet.bmp")
local heart   = lt.load_texture("heart.bmp")
local ground  = lt.plane(8)
local cube    = lt.cube()
local drum    = lt.cylinder(14)
local ball    = lt.sphere(10)

-- ── character meshes: chunky low-poly figures built from cuboids ────────
local function cuboid(t, cx, cy, cz, w, h, d, r, g, b)
  local x0, x1 = cx - w / 2, cx + w / 2
  local y0, y1 = cy - h / 2, cy + h / 2
  local z0, z1 = cz - d / 2, cz + d / 2
  local function quad(ax,ay,az, bx,by,bz, cx2,cy2,cz2, dx,dy,dz, nx,ny,nz)
    local pts = {{ax,ay,az},{bx,by,bz},{cx2,cy2,cz2},
                 {ax,ay,az},{cx2,cy2,cz2},{dx,dy,dz}}
    for _, p in ipairs(pts) do
      t[#t+1]=p[1]; t[#t+1]=p[2]; t[#t+1]=p[3]
      t[#t+1]=nx;   t[#t+1]=ny;   t[#t+1]=nz
      t[#t+1]=0;    t[#t+1]=0
      t[#t+1]=r;    t[#t+1]=g;    t[#t+1]=b;  t[#t+1]=1
    end
  end
  quad(x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1,  0,0,1)
  quad(x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0,  0,0,-1)
  quad(x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1,  1,0,0)
  quad(x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, -1,0,0)
  quad(x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0,  0,1,0)
  quad(x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1,  0,-1,0)
end

-- hero pose: s = -1..1 leg/arm swing (keyframes for lt.draw_lerp)
local function hero_pose(s)
  local t = {}
  local skin  = {0.90, 0.72, 0.55}
  local tunic = {0.30, 0.62, 0.28}
  local pants = {0.42, 0.30, 0.20}
  local hair  = {0.32, 0.20, 0.12}
  cuboid(t, 0, 0.80, 0, 0.30, 0.26, 0.28, skin[1], skin[2], skin[3])   -- head
  cuboid(t, 0, 0.955, 0, 0.34, 0.09, 0.32, hair[1], hair[2], hair[3])  -- hair
  cuboid(t, 0, 0.50, 0, 0.36, 0.36, 0.24, tunic[1], tunic[2], tunic[3])-- tunic
  cuboid(t, -0.10, 0.16, 0.13 * s, 0.13, 0.32, 0.15,
         pants[1], pants[2], pants[3])                                 -- leg L
  cuboid(t, 0.10, 0.16, -0.13 * s, 0.13, 0.32, 0.15,
         pants[1], pants[2], pants[3])                                 -- leg R
  cuboid(t, -0.245, 0.50, -0.12 * s, 0.10, 0.32, 0.13,
         tunic[1], tunic[2], tunic[3])                                 -- arm L
  cuboid(t, 0.245, 0.50, 0.12 * s, 0.10, 0.32, 0.13,
         tunic[1], tunic[2], tunic[3])                                 -- arm R
  return lt.mesh(t)
end
local heroA, heroB = hero_pose(1), hero_pose(-1)

-- the shopkeeper: round purple fellow (a nod to the reference's merchant)
local function shopkeeper()
  local t = {}
  cuboid(t, 0, 0.42, 0, 0.52, 0.5, 0.42, 0.55, 0.30, 0.62)   -- body
  cuboid(t, 0, 0.86, 0, 0.44, 0.4, 0.40, 0.62, 0.36, 0.70)   -- big head
  cuboid(t, -0.26, 1.06, 0, 0.10, 0.22, 0.10, 0.62, 0.36, 0.70) -- ear L
  cuboid(t, 0.26, 1.06, 0, 0.10, 0.22, 0.10, 0.62, 0.36, 0.70)  -- ear R
  cuboid(t, -0.09, 0.90, 0.205, 0.07, 0.07, 0.02, 0.1, 0.08, 0.1) -- eye L
  cuboid(t, 0.09, 0.90, 0.205, 0.07, 0.07, 0.02, 0.1, 0.08, 0.1)  -- eye R
  return lt.mesh(t)
end
local keeper = shopkeeper()

-- ── state ────────────────────────────────────────────────────────────────
local px, pz, pry = 0, 1.6, 0        -- player pos + facing
local walk, moving = 0, false
local AUTO = os.getenv("ROOM_AUTO") ~= nil  -- verification: walk a circle

function update(dt)
  local dx, dz = 0, 0
  if AUTO then dx, dz = math.cos(lt.time()), math.sin(lt.time()) end
  if lt.key("left")  then dx = dx - 1 end
  if lt.key("right") then dx = dx + 1 end
  if lt.key("up")    then dz = dz - 1 end
  if lt.key("down")  then dz = dz + 1 end
  moving = dx ~= 0 or dz ~= 0
  if moving then
    local l = math.sqrt(dx * dx + dz * dz)
    px = math.max(-4.6, math.min(4.6, px + dx / l * 3.0 * dt))
    pz = math.max(-2.6, math.min(3.1, pz + dz / l * 3.0 * dt))
    pry = math.atan(dx, dz)          -- face travel direction
    walk = walk + dt * 9
  end
end

local function furniture()
  -- long red table along the top wall, with plates
  lt.draw(cube, 0, 0.42, -3.05, 0, 0, 0, 4.6, 0.18, 0.9, 0.66, 0.13, 0.11)
  lt.draw(cube, -2.0, 0.16, -3.05, 0, 0, 0, 0.24, 0.34, 0.7, 0.42, 0.09, 0.08)
  lt.draw(cube, 2.0, 0.16, -3.05, 0, 0, 0, 0.24, 0.34, 0.7, 0.42, 0.09, 0.08)
  for _, x in ipairs({-1.6, 0, 1.6}) do
    lt.draw(drum, x, 0.545, -3.0, 0, 0, 0, 0.42, 0.05, 0.42, 0.94, 0.92, 0.88)
  end
  -- red benches down the left and right walls, plates + goods on top
  for _, side in ipairs({-1, 1}) do
    local bx = side * 4.45
    lt.draw(cube, bx, 0.38, 0.2, 0, 0, 0, 0.9, 0.16, 4.4, 0.66, 0.13, 0.11)
    lt.draw(cube, bx, 0.15, -1.6, 0, 0, 0, 0.7, 0.3, 0.24, 0.42, 0.09, 0.08)
    lt.draw(cube, bx, 0.15, 2.0, 0, 0, 0, 0.7, 0.3, 0.24, 0.42, 0.09, 0.08)
    lt.draw(drum, bx, 0.485, -1.0, 0, 0, 0, 0.4, 0.05, 0.4, 0.94, 0.92, 0.88)
    lt.draw(drum, bx, 0.485, 1.4, 0, 0, 0, 0.4, 0.05, 0.4, 0.94, 0.92, 0.88)
    lt.draw(cube, bx, 0.55, 0.2, 0, 0.5, 0, 0.28, 0.18, 0.28,
            0.85, 0.55, 0.15)                       -- a wrapped parcel
  end
  -- bed, top-right corner: frame, red blanket, white pillow
  lt.draw(cube, 3.6, 0.22, -2.6, 0, 0, 0, 1.5, 0.28, 1.0, 0.50, 0.34, 0.22)
  lt.draw(cube, 3.75, 0.38, -2.6, 0, 0, 0, 1.1, 0.10, 0.9, 0.66, 0.13, 0.11)
  lt.draw(cube, 3.05, 0.40, -2.6, 0, 0, 0, 0.42, 0.12, 0.8, 0.94, 0.92, 0.88)
  -- barrels, bottom-left cluster
  lt.draw(drum, -3.9, 0.32, 2.9, 0, 0, 0, 0.6, 0.64, 0.6, 0.48, 0.34, 0.20)
  lt.draw(drum, -3.15, 0.32, 3.15, 0, 0.7, 0, 0.6, 0.64, 0.6, 0.52, 0.37, 0.22)
  -- potted plants, bottom middle-right (pot + leafy ball)
  for _, x in ipairs({1.1, 3.3}) do
    lt.draw(drum, x, 0.16, 3.1, 0, 0, 0, 0.36, 0.3, 0.36, 0.55, 0.30, 0.18)
    lt.draw(ball, x, 0.48, 3.1, 0, x, 0, 0.55, 0.5, 0.55, 0.25, 0.52, 0.20)
  end
end

function draw()
  lt.clear(0.06, 0.045, 0.04)
  lt.fog(0, -1, 0, 0, 0)               -- interior: fog off
  lt.light(0.35, -1.0, -0.25, 0.52)    -- soft key light + high ambient
  -- warm shop light in the middle + a lamp over the counter
  lt.point_light(0, 0, 2.6, 0.3, 8.5, 0.95, 0.62, 0.30)
  lt.point_light(1, 0, 1.8, -2.8, 5.0, 0.9, 0.55, 0.25)
  lt.point_light(2, 0, 0, 0, 0, 0, 0, 0)
  lt.point_light(3, 0, 0, 0, 0, 0, 0, 0)

  -- near-overhead ALBW camera, fixed on the room
  lt.camera(0, 8.2, 6.2, 0, -0.25, 0.35, 42)

  -- floor (plank texture), carpet decal floating just above it
  lt.draw(ground, 0, 0, 0.4, 0, 0, 0, 10.4, 1, 8.4, 1, 1, 1, floorT)
  lt.draw(ground, 0, 0.015, 0.35, 0, 0, 0, 4.4, 1, 3.6, 1, 1, 1, carpetT)

  -- walls (textured), drawn as thin slabs around the floor
  lt.draw(cube, 0, 1.1, -3.72, 0, 0, 0, 11.6, 2.2, 0.35, 1, 1, 1, wallT)
  lt.draw(cube, -5.35, 1.1, 0.6, 0, 0, 0, 0.35, 2.2, 8.6, 1, 1, 1, wallT)
  lt.draw(cube, 5.35, 1.1, 0.6, 0, 0, 0, 0.35, 2.2, 8.6, 1, 1, 1, wallT)

  furniture()

  -- shopkeeper behind the counter, breathing gently
  local breathe = 1 + 0.02 * math.sin(lt.time() * 2.2)
  lt.shadow(0.9, 0.03, -2.2, 0.42, 0.4)
  lt.draw(keeper, 0.9, 0, -2.2, 0, 0, 0, 1, breathe, 1)

  -- the hero: keyframe-tweened walk (draw_lerp), grounded by a shadow
  local t = moving and (0.5 + 0.5 * math.sin(walk)) or 0.5
  lt.shadow(px, 0.03, pz, 0.38, 0.42)
  lt.draw_lerp(heroA, heroB, t, px, 0, pz, 0, pry, 0, 1.15, 1.15, 1.15)

  -- ── HUD: hearts + item panel (2D over everything) ──
  for i = 0, 2 do lt.sprite(heart, 6 + i * 10, 5) end
  lt.rect(4, 40, 16, 116, 0.16, 0.10, 0.24, 0.85)      -- item panel
  lt.rect(4, 40, 16, 2, 0.98, 0.80, 0.25)
  lt.rect(4, 154, 16, 2, 0.98, 0.80, 0.25)
  lt.rect(7, 48, 10, 10, 0.30, 0.62, 0.28)             -- equipped items
  lt.rect(7, 64, 10, 10, 0.85, 0.55, 0.15)
  lt.rect(7, 80, 10, 10, 0.55, 0.30, 0.62)
  lt.print("THE SHOP", lt.W - 8 * 8 - 4, lt.H - 11, 1, 1, 1, 0.75)
end
