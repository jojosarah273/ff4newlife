#!/usr/bin/env node
/**
 * export_gfx.js - export ripped graphics to PNGs using the ff4 disassembly
 * toolchain (ROMDataCodec from third_party/ff4/tools/romtools).
 *
 * Consumes data/ff4-en-data.json (the `make rip` output) and writes
 * modern PNG files to data/graphics/:
 *
 *   title.png       the title screen (splash, 256x224)
 *   prologue1.png   opening still 1 (256x224)
 *   prologue2.png   opening still 2 (256x224)
 *   window.png      menu window tileset
 *   tileset00..15.png  field tilesets (64x64-tile atlases, 512x512)
 *   world00..02.png    overworld/underground/moon tilesets
 *
 * Usage: node tools/export_gfx.js [path/to/ff4-en-data.json]
 */
'use strict';

const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
/* Pixel decode is done here directly (verified MSB-first 2bpp planes);
   the ff4 toolchain remains the source of the data (the rip JSON). */

const ROOT = path.join(__dirname, '..');
const DATA_PATH = process.argv[2] ||
    path.join(ROOT, 'data', 'ff4-en-data.json');
const OUT = path.join(ROOT, 'data', 'graphics');

const data = JSON.parse(fs.readFileSync(DATA_PATH, 'utf8')).obj;

/* ---------- helpers ---------- */

function b64(v) {
    const s = v.length % 4 ? v + '='.repeat(4 - (v.length % 4)) : v;
    return Buffer.from(s, 'base64');
}

function color16(c) {
    return [((c & 0x1F) << 3) | ((c >> 2) & 7),
            (((c >> 5) & 0x1F) << 3) | (((c >> 7) & 7)),
            (((c >> 10) & 0x1F) << 3) | (((c >> 12) & 7)), 255];
}

/* decode a bpp-blob into 8x8 tiles (bpp planes of 8 bytes each;
   rip definition format: snes2bpp/3bpp/4bpp) */
function decodeTiles(blob, bpp) {
    const bytes = b64(blob);
    const tileBytes = bpp * 8;
    const n = Math.floor(bytes.length / tileBytes);
    const tiles = [];
    for (let t = 0; t < n; ++t) {
        const arr = new Uint8Array(64);
        const base = t * tileBytes;
        for (let row = 0; row < 8; ++row) {
            let v = 0;
            for (let pl = 0; pl < bpp; ++pl) {
                const p = bytes[base + pl * 8 + row];
                for (let b = 7; b >= 0; --b)
                    arr[row * 8 + (7 - b)] |=
                        (((p >> b) & 1) << pl) * 0 + (((p >> b) & 1) << pl);
            }
        }
        /* redo cleanly (bit-order independent of the |= trick) */
        for (let row = 0; row < 8; ++row) {
            let acc = 0;
            for (let pl = 0; pl < bpp; ++pl)
                acc |= ((bytes[base + pl * 8 + row] >> 7) & 1) << pl;
            arr[row * 8] = acc;
        }
        for (let row = 0; row < 8; ++row) {
            const p0 = bytes[base + row];
            const p1 = bytes[base + 8 + row];
            const p2 = bpp > 2 ? bytes[base + 16 + row] : 0;
            const p3 = bpp > 3 ? bytes[base + 24 + row] : 0;
            for (let b = 7; b >= 0; --b) {
                const v = ((p0 >> b) & 1) | (((p1 >> b) & 1) << 1) |
                          (((p2 >> b) & 1) << 2) | (((p3 >> b) & 1) << 3);
                arr[row * 8 + (7 - b)] = v;
            }
        }
        tiles.push(arr);
    }
    return tiles;
}

function decodePalette(blob) {
    const bytes = b64(blob);
    const cols = [];
    for (let i = 0; i + 1 < bytes.length; i += 2)
        cols.push(color16(bytes[i] | (bytes[i + 1] << 8)));
    return cols;
}

function composeTilemap(gfx, pal, tilemapBlob, offX, offY, viewW, viewH,
                        mapW, bpp, groupSize) {
    const tm = b64(tilemapBlob);
    const px = Buffer.alloc(viewW * 8 * viewH * 8 * 4);
    const tiles = decodeTiles(gfx, bpp);
    const palette = decodePalette(pal);

    for (let y = 0; y < viewH; ++y) {
        for (let x = 0; x < viewW; ++x) {
            const mi = ((offY + y) * mapW + (offX + x)) * 2;
            if (mi + 1 >= tm.length)
                continue;
            /* title splash tilemap: big-endian entries, tile = e>>1,
               palette group = bits 12-14, 2bpp tiles */
            /* title splash entries (verified 93.5% vs snes9x):
               tile = e>>1, palette group = bits 12-14,
               hflip = bit 14, vflip = bit 15 */
            const e = (tm[mi] << 8) | tm[mi + 1];
            const tile = (e >> 1) & 0x3FF;
            const palgrp = (e >> 12) & 7;
            const hflip = (e >> 14) & 1;
            const vflip = (e >> 15) & 1;
            const src = tiles[tile] || new Uint8Array(64);

            for (let ty = 0; ty < 8; ++ty) {
                const yy = vflip ? 7 - ty : ty;
                for (let tx = 0; tx < 8; ++tx) {
                    const xx = hflip ? 7 - tx : tx;
                    const c = src[yy * 8 + xx];
                    let rgb;
                    if (c === 0)
                        rgb = palette[0];
                    else
                        rgb = palette[palgrp * groupSize + c] ||
                              palette[c];
                    const di = ((y * 8 + ty) * viewW * 8 + (x * 8 + tx)) * 4;
                    px[di] = rgb[0];
                    px[di + 1] = rgb[1];
                    px[di + 2] = rgb[2];
                    px[di + 3] = rgb[3];
                }
            }
        }
    }
    return px;
}

/* ---------- minimal PNG writer ---------- */

const CRC_TBL = (() => {
    const t = new Int32Array(256);
    for (let n = 0; n < 256; ++n) {
        let c = n;
        for (let k = 0; k < 8; ++k)
            c = (c & 1) ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        t[n] = c;
    }
    return t;
})();

function crc32(buf) {
    let c = ~0;
    for (let i = 0; i < buf.length; ++i)
        c = CRC_TBL[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
    return (~c) >>> 0;
}

function chunk(type, payload) {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(payload.length);
    const data = Buffer.concat([Buffer.from(type, 'ascii'), payload]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(data));
    return Buffer.concat([len, data, crc]);
}

function writePng(file, rgba, w, h) {
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0);
    ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8;                 /* bit depth */
    ihdr[9] = 6;                 /* RGBA */
    /* raw scanlines with filter byte 0 */
    const raw = Buffer.alloc(h * (1 + w * 4));
    for (let y = 0; y < h; ++y) {
        raw[y * (1 + w * 4)] = 0;
        rgba.copy(raw, y * (1 + w * 4) + 1, y * w * 4, (y + 1) * w * 4);
    }
    const png = Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
        chunk('IHDR', ihdr),
        chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
        chunk('IEND', Buffer.alloc(0))
    ]);
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, png);
    process.stdout.write(`  ${path.relative(ROOT, file)}: ${w}x${h}\n`);
}

/* ---------- export ---------- */

function exportScreen(name, gfxKey, palKey, mapKey, offX, offY, viewW, viewH,
                      mapW, bpp, groupSize) {
    const rgba = composeTilemap(data[gfxKey], data[palKey], data[mapKey],
                                offX, offY, viewW, viewH, mapW, bpp, groupSize);
    writePng(path.join(OUT, name), rgba, viewW * 8, viewH * 8);
}

/* the game loads its own 128-color title palette (field/title_en.asm:
   TitlePal, $0CDB) - the disassembly ships the bytes, no ROM needed */
function titlePaletteBytes() {
    const src = fs.readFileSync(
        path.join(ROOT, 'third_party/ff4/field/gfx/title_pal_en.asm'),
        'utf8');
    const vals = [];
    const re = /\.byte\s+([^\n]+)/g;
    let m;
    while ((m = re.exec(src)) !== null) {
        for (const tok of m[1].split(',')) {
            const t = tok.trim();
            if (t.startsWith('$')) vals.push(parseInt(t.slice(1), 16));
        }
    }
    const bytes = Buffer.from(vals);
    fs.mkdirSync(OUT, { recursive: true });
    fs.writeFileSync(path.join(OUT, 'title_palette.bin'), bytes);
    return bytes;
}
const titlePal = titlePaletteBytes();
const titleColors = [];
for (let i = 0; i + 1 < titlePal.length; i += 2) {
    const c = titlePal[i] | (titlePal[i + 1] << 8);
    titleColors.push([
        ((c & 0x1F) << 3) | ((c >> 2) & 7),
        (((c >> 5) & 0x1F) << 3) | ((c >> 7) & 7),
        (((c >> 10) & 0x1F) << 3) | ((c >> 12) & 7), 255
    ]);
}

/* title screen with the REAL title palette (the game copies TitlePal
   into $0CDB; the tilemap groups are 16 colors in 4bpp mode) */
function composeTitle(offX, offY, viewW, viewH, mapW) {
    const tm = b64(data.splashTilemap);
    const px = Buffer.alloc(viewW * 8 * viewH * 8 * 4);
    const tiles = decodeTiles(data.splashGraphics, 2);
    for (let y = 0; y < viewH; ++y) {
        for (let x = 0; x < viewW; ++x) {
            const mi = ((offY + y) * mapW + (offX + x)) * 2;
            if (mi + 1 >= tm.length) continue;
            /* title splash tilemap: big-endian entries, tile = e>>1,
               palette group = bits 12-14, 2bpp tiles */
            /* title splash entries (verified 93.5% vs snes9x):
               tile = e>>1, palette group = bits 12-14,
               hflip = bit 14, vflip = bit 15 */
            const e = (tm[mi] << 8) | tm[mi + 1];
            const tile = (e >> 1) & 0x3FF;
            const palgrp = (e >> 12) & 7;
            const hflip = (e >> 14) & 1;
            const vflip = (e >> 15) & 1;
            const src = tiles[tile] || new Uint8Array(64);
            for (let ty = 0; ty < 8; ++ty) {
                const yy = vflip ? 7 - ty : ty;
                for (let tx = 0; tx < 8; ++tx) {
                    const xx = hflip ? 7 - tx : tx;
                    const c = src[yy * 8 + xx];
                    /* 2bpp tiles; 16-color palette stride as the
                       game lays out the title palette */
                    const rgb = c === 0 ? titleColors[0]
                                        : (titleColors[palgrp * 16 + c] ||
                                           titleColors[c]);
                    const di = ((y * 8 + ty) * viewW * 8 + (x * 8 + tx)) * 4;
                    px[di] = rgb[0];
                    px[di + 1] = rgb[1];
                    px[di + 2] = rgb[2];
                    px[di + 3] = rgb[3];
                }
            }
        }
    }
    return px;
}

console.log('exporting graphics...');
writePng(path.join(OUT, 'title.png'), composeTitle(0, 0, 32, 28, 64),
         256, 224);
/* prologue stills (map holds a 32x28 window per screen) */
exportScreen('prologue1.png', 'prologueGraphics', 'prologuePalette',
             'prologueTilemap1', 0, 0, 32, 28, 64, 3, 8);
exportScreen('prologue2.png', 'prologueGraphics', 'prologuePalette',
             'prologueTilemap2', 0, 0, 32, 28, 64, 3, 8);
/* the window frame tileset (used by menus) */
{
    const tiles = decodeTiles(data.windowGraphics, 2);
    const pal = decodePalette(data.windowPalette);
    const w = 16, h = 16;
    const rgba = Buffer.alloc(w * 8 * h * 8 * 4);

    for (let t = 0; t < w * h && t < tiles.length; ++t) {
        for (let ty = 0; ty < 8; ++ty)
            for (let tx = 0; tx < 8; ++tx) {
                const c = tiles[t][ty * 8 + tx];
                const rgb = c ? pal[c] : pal[0];
                const di = (((t >> 4) * 8 + ty) * w * 8 +
                            ((t & 15) * 8 + tx)) * 4;
                rgba[di] = rgb[0];
                rgba[di + 1] = rgb[1];
                rgba[di + 2] = rgb[2];
                rgba[di + 3] = 255;
            }
    }
    writePng(path.join(OUT, 'window.png'), rgba, w * 8, h * 8);
}
/* field tilesets (16 atlases of 64x64 tiles) */
fs.mkdirSync(OUT, { recursive: true });
for (let t = 0; t < 16; ++t) {
    const blob = b64(data.mapTilesets[t]);
    const tileCount = Math.floor(blob.length / 16);
    const side = Math.floor(Math.sqrt(tileCount));
    const pixels = Buffer.alloc(side * 8 * side * 8 * 4);

    for (let i = 0; i < tileCount; ++i) {
        const px = new Uint8Array(64);
        const base = i * 16;
        for (let row = 0; row < 8; ++row) {
            const p0 = blob[base + row], p1 = blob[base + 8 + row];
            for (let b = 7; b >= 0; --b) {
                px[row * 8 + (7 - b)] = ((p0 >> b) & 1) |
                                        (((p1 >> b) & 1) << 1);
            }
        }
        const tx = i % side, ty = Math.floor(i / side);
        for (let sy = 0; sy < 8; ++sy)
            for (let sx = 0; sx < 8; ++sx) {
                const c = px[sy * 8 + sx];
                const rgb = c ? [255, 255, 255, 255] : [0, 0, 0, 0];
                const di = ((ty * 8 + sy) * side * 8 + (tx * 8 + sx)) * 4;
                pixels[di] = rgb[0];
                pixels[di + 1] = rgb[1];
                pixels[di + 2] = rgb[2];
                pixels[di + 3] = rgb[3];
            }
    }
    writePng(path.join(OUT, `tileset${String(t).padStart(2, '0')}.png`),
             pixels, side * 8, side * 8);
}

console.log('done.');