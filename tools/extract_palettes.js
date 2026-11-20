#!/usr/bin/env node
/* extract_palettes.js
 * The rip JSON's field-palette slices are wrong for rendering (the
 * mapPalettes items don't match the disassembly data). The authoritative
 * tables are the localized .asm include files; this tool parses their
 * .byte streams and writes plain LE binaries to data/graphics/.
 *
 *   node tools/extract_palettes.js [ff4dir]
 */
const fs = require('fs');
const path = require('path');

const FF4 = process.argv[2] || 'third_party/ff4';

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

const targets = [
    ['mapPal',     'field/gfx/map_pal_en.asm',      'map_pal.bin'],
    ['worldPal',   'field/gfx/world_pal.asm',       'world_pal.bin'],
    ['spritePal',  'field/gfx/map_sprite_pal.asm',  'map_sprite_pal.bin'],
    ['windowPal',  'menu/gfx/window_pal.asm',       'window_pal.bin'],
    ['titlePal',   'field/gfx/title_pal_en.asm',    'title_palette.bin'],
];

const dir = 'data/graphics';
if (!fs.existsSync(dir)) fs.mkdirSync(dir, { recursive: true });
for (const [name, file, outName] of targets) {
    try {
        const buf = parseBytes(file);
        fs.writeFileSync(path.join(dir, outName), buf);
        console.log('%s <- %s (%d bytes)', outName, file, buf.length);
    } catch (e) {
        console.error('skip %s: %s', file, e.message);
    }
}