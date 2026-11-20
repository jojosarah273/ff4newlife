/**
 * @file ff4_dialog_layout.h
 * @brief Message-box geometry and tiles - MEASURED, DO NOT CHANGE.
 *
 * Locked against a real snes9x frame (256x224) and the ASM:
 *
 *   field/window.asm  OpenDlgWindow  @b30b
 *       $df = 1, then inc per two IRQs until $df == 8
 *       -> $df is the number of BODY rows; the box is drawn in top to bottom.
 *   field/window.asm  CloseDlgWindow @b39e
 *       dec $df to 0 after $ec clears the text (text out, then window out).
 *   field/window.asm  TfrDlgWindow   @b75f
 *       three 32-entry BG3 strips (30 visible + 2 pad), entry {tile, $20}:
 *           top    16 17*28 18
 *           body   19 FF*28 1A
 *           bottom 1B 1C*28 1D
 *   field/window.asm  DecodeDlgText  @b21f
 *       the decoded byte IS the tilemap tile number, so glyph tile == text code.
 *
 * Measured on the emulator frame (tools/retro_dump.c, frame 2100):
 *   border white lines   y = 17 (top) and 89 (bottom)
 *   border white columns x = 9 (left) and 246 (right)
 *   text line tops       y = 27, 43, 59, 75   (pitch 16)
 *   text left            x = 24
 *
 * Anything that changes these numbers is a regression.
 */
#ifndef FF4_DIALOG_LAYOUT_H
#define FF4_DIALOG_LAYOUT_H

/* --- box ----------------------------------------------------------------- */
#define FF4_DLG_X            8     /**< left edge, screen px                    */
#define FF4_DLG_W          240     /**< 30 tiles                                */
#define FF4_DLG_TOP_Y       12     /**< first tile ROW of the box               */
#define FF4_DLG_BODY_ROWS    8     /**< $df maximum (body rows between borders) */
#define FF4_DLG_BORDER_ROWS  1     /**< one above, one below                    */
#define FF4_DLG_ROWS        (FF4_DLG_BODY_ROWS + 2 * FF4_DLG_BORDER_ROWS)

/* --- text ---------------------------------------------------------------- */
#define FF4_DLG_TEXT_X      24     /**< = FF4_DLG_X + 2 tiles                   */
#define FF4_DLG_TEXT_Y0     27     /**< first line, screen px                   */
#define FF4_DLG_TEXT_PITCH  16     /**< two tile rows between lines             */
#define FF4_DLG_COLS        26     /**< columns inside the borders              */
#define FF4_DLG_LINES        4     /**< four text lines per page                */

/* --- tiles (BG3 page at VRAM $2000, WindowGfx @ROM 0x57000) -------------- */
#define FF4_DLG_TILE_TL   0x16
#define FF4_DLG_TILE_TOP  0x17
#define FF4_DLG_TILE_TR   0x18
#define FF4_DLG_TILE_L    0x19
#define FF4_DLG_TILE_BODY 0xFF
#define FF4_DLG_TILE_R    0x1A
#define FF4_DLG_TILE_BL   0x1B
#define FF4_DLG_TILE_BOT  0x1C
#define FF4_DLG_TILE_BR   0x1D

/* --- ROM / asset locations ---------------------------------------------- */
#define FF4_WINDOW_GFX_OFF 0x57000  /**< WindowGfx, 4096 B, 256 x 2bpp        */
#define FF4_WINDOW_PAL_OFF 0x687D0  /**< WindowPal, 32 B (16 colours BGR555) */

#endif /* FF4_DIALOG_LAYOUT_H */
