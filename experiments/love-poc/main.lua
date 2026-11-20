-- FF4 feasibility PoC: decode a real SNES field map (Baron, map 36) from
-- the resource bundle in Lua and render it on a 256x224 canvas.
--
-- run:  love experiments/love-poc /abs/path/to/FF4_ASMTOC
local ROOT = nil
for _, a in ipairs(arg or {}) do
    if a:match("^/") then ROOT = a end
end
ROOT = ROOT or "."
local GFX = ROOT .. "/data/gfx"

local function readbin(rel)
    local f = io.open(GFX .. "/" .. rel, "rb")
    if not f then return nil end
    local d = f:read("*a"); f:close()
    return d
end

-- RLE layout (ff4-map): [0x80|value][count] = value repeated count+1
local function decode_layout(blob)
    local out, o = {}, 1
    local i, n = 1, #blob
    while i <= n and o <= 32 * 256 do
        local b = blob:byte(i); i = i + 1
        if b >= 0x80 then
            local cnt = blob:byte(i) + 1; i = i + 1
            for _ = 1, cnt do
                if o > 32 * 256 then break end
                out[o] = b - 0x80; o = o + 1
            end
        else
            out[o] = b; o = o + 1
        end
    end
    return out
end

local function color16(pal, idx)
    local lo = pal:byte(idx * 2 + 1) or 0
    local hi = pal:byte(idx * 2 + 2) or 0
    local c = lo + hi * 256
    local r = (c % 32) * 8 + math.floor((c % 32) / 4)
    local g = (math.floor(c / 32) % 32) * 8 + math.floor((math.floor(c / 32) % 32) / 4)
    local bl = (math.floor(c / 1024) % 32) * 8 + math.floor((math.floor(c / 1024) % 32) / 4)
    return r / 255, g / 255, bl / 255
end

local stats = { tiles = 0, tiles_out = 0, decode_ms = 0 }

local function build_field(map, camx, camy)
    local t0 = love.timer.getTime()
    local layout = decode_layout(readbin(("gfx/mapLayouts1[%d].bin"):format(map)))
    local ts = readbin("gfx/mapTilesets[3].bin")
    local gfx = readbin("gfx/mapGraphics3.bin")
    local flat = readbin("pal/map_pal.bin")
    -- LoadMapPal projection for palette id 3
    local pal = {}
    local x0 = 3 * 128
    for i = 0, 127 do
        pal[i] = (flat:byte(0x11 + x0 + i) or 0) + (flat:byte(0x91 + x0 + i) or 0) * 256
    end

    local W, H = 256, 224
    local img = love.image.newImageData(W, H)
    img:mapPixel(function() return 0, 0, 0, 1 end)

    for ty = 0, 27 do
        for tx = 0, 31 do
            local b = layout[((camy + ty) % 256) * 32 + ((camx + tx) % 32) + 1]
            local lo = ts:byte(b + 1) or 0
            local hi = ts:byte(b + 256 + 1) or 0
            local e = lo + hi * 256
            local tile = e % 1024
            local grp = math.floor(tile / 16) % 8
            local base = tile * 24
            stats.tiles = stats.tiles + 1
            if base + 24 <= #gfx then
                stats.tiles_out = stats.tiles_out + 1
                for row = 0, 7 do
                    local p0 = gfx:byte(base + row + 1)
                    local p1 = gfx:byte(base + 8 + row + 1)
                    local p2 = gfx:byte(base + 16 + row + 1)
                    for bit = 7, 0, -1 do
                        local c = (math.floor(p0 / 2 ^ bit) % 2)
                              + (math.floor(p1 / 2 ^ bit) % 2) * 2
                              + (math.floor(p2 / 2 ^ bit) % 2) * 4
                        local r, g, bl = color16(flat, (grp * 16 + c) % 128)
                        img:setPixel(tx * 8 + (7 - bit), ty * 8 + row, r, g, bl, 1)
                    end
                end
            end
        end
    end
    stats.decode_ms = (love.timer.getTime() - t0) * 1000
    return img
end

local img, frame

function love.load()
    love.graphics.setDefaultFilter("nearest", "nearest")
    love.window.setMode(768, 672)
    img = build_field(36, 0, 16)
    frame = 0
    print(("[poc] decoded %d tiles (%d in-range) in %.1f ms")
          :format(stats.tiles, stats.tiles_out, stats.decode_ms))
end

function love.draw()
    love.graphics.draw(img, 0, 0, 0, 3, 3)
    frame = frame + 1
    if frame % 60 == 0 then
        local fps = love.timer.getFPS()
        print(("[poc] frame %d  fps=%.0f  mem=%.1f MB")
              :format(frame, fps, collectgarbage("count") / 1024))
    end
    if os.getenv("POC_QUIT") and frame >= tonumber(os.getenv("POC_FRAMES") or 120) then
        love.event.quit()
    end
end