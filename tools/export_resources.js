#!/usr/bin/env node
/* export_resources.js
 * Builds the resource bundle the engine consumes: data/gfx/
 *
 *   - manifest.json          : name -> {type, size, sha256?, source}
 *   - pal/*.bin              : authoritative palettes parsed from the
 *                              disassembly .asm files (the rip JSON's
 *                              mapPalettes is NOT the field palette)
 *   - map_pal.bin            : the flat MapPal table (4096 bytes)
 *   - gfx/*.bin              : raw rip blobs mirrored from the JSON
 *
 * usage: node tools/export_resources.js [ff4dir] [outdir]
 */
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const FF4 = process.argv[2] || 'third_party/ff4';
const OUT = process.argv[3] || 'data/gfx';
const RIP = path.join(FF4, 'ff4-en-data.json');

/* ---- .asm byte parser ---- */
function parseBytes(file) {
    const src = fs.readFileSync(path.join(FF4, file), 'utf8');
    const out = [];
    for (const m of src.matchAll(/\.byte\s+([^\n]+)/g)) {
        for (const tok of m[1].split(',')) {
            const t = tok.trim();
            if (t.startsWith('$')) out.push(parseInt(t.slice(1), 16) & 0xFF);
        }
    }
    return Buffer.from(out);
}

const manifest = [];
function add(name, type, buf, source) {
    const dir = path.dirname(path.join(OUT, name));
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(OUT, name), buf);
    manifest.push({
        name,
        type,
        size: buf.length,
        sha256: crypto.createHash('sha256').update(buf).digest('hex').slice(0, 16),
        source,
    });
}

/* ---- authoritative palettes from the disassembly ---- */
const palAsm = [
    ['pal/map_pal.bin',            'field/gfx/map_pal_en.asm',      'mapPal'],
    ['pal/world_pal.bin',          'field/gfx/world_pal.asm',       'worldPal'],
    ['pal/map_sprite_pal.bin',     'field/gfx/map_sprite_pal.asm',  'spritePal'],
    ['pal/window_pal.bin',         'menu/gfx/window_pal.asm',       'windowPal'],
    ['pal/title_palette.bin',      'field/gfx/title_pal_en.asm',    'titlePal'],
    ['pal/prologue_pal.bin',       'field/gfx/prologue_pal.asm',    'prologuePal'],
    ['pal/portrait_pal.bin',       'menu/gfx/portrait_pal.asm',     'portraitPal'],
    ['pal/earth_moon_pal.bin',     'field/gfx/earth_moon_pal.asm',  'earthMoonPal'],
];
for (const [name, file, type] of palAsm) {
    try {
        add(name, type, parseBytes(file), `disasm::${file}`);
    } catch (e) {
        console.error('skip %s: %s', file, e.message);
    }
}

/* ---- mirror the rip blobs (graphics, tilemaps, sprites, layout...) ---- */
const ripData = JSON.parse(fs.readFileSync(RIP, 'utf8'));
const rip = ripData.obj || ripData;
let mirrored = 0;
for (const [key, value] of Object.entries(rip)) {
    const wants = /Graphics|Palette|Layout|Tileset|Tilemap|Sprite|Script|Sine/i.test(key);
    if (!wants) continue;
    if (typeof value === 'string') {
        const buf = Buffer.from(value, 'base64');
        add(`gfx/${key}.bin`, 'blob', buf, `rip::${key}`);
        ++mirrored;
    } else if (Array.isArray(value)) {
        for (let i = 0; i < value.length; ++i) {
            if (typeof value[i] !== 'string') continue;
            const buf = Buffer.from(value[i], 'base64');
            add(`gfx/${key}[${i}].bin`, 'blob', buf, `rip::${key}[${i}]`);
            ++mirrored;
        }
    }
}

/* ---- manifest ---- */
add('manifest.json', 'manifest', Buffer.from(JSON.stringify(manifest, null, 1)),
    'generated');
/* Window tiles: the rip's `windowGraphics` is NOT the game's WindowGfx
   (it is offset and lacks tiles 0x16..0x1D), so parse the disassembly's
   menu/gfx/window_gfx_en.asm instead: 0x1000 bytes = 256 2bpp tiles. */
try {
    add('gfx/window_gfx.bin', 'windowGfx',
        parseBytes('menu/gfx/window_gfx_en.asm'), 'disasm::window_gfx_en.asm');
} catch (e) {
    console.error('skip window_gfx: %s', e.message);
}

console.log('bundle: %d files (%d rip blobs mirrored) -> %s',
            manifest.length, mirrored, OUT);