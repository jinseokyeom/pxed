#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"

/* ================================================================
   constants
   ================================================================ */

#define CELL_PX   6
#define MIN_DIM   1
#define MAX_DIM   1024
#define MIN_SCALE 0.1f
#define MAX_SCALE 64.0f
#define HEADER    "PX"

/* ================================================================
   types
   ================================================================ */

typedef enum Tool {
    TOOL_PENCIL    = 0,
    TOOL_ERASER    = 1,
    TOOL_SELECT    = 2,
    TOOL_CLIPBOARD = 3
} Tool;

typedef struct {
    int      x, y;
    int      w, h;
    uint8_t *data;
} Clipboard;

typedef struct {
    Camera2D  cam;
    float     initial_zoom;
    int       win_w, win_h;
    int       canvas_w, canvas_h;
    int       cell_px;
    Tool      active_tool;
    int       brush_size;
    int       show_grid;
    int       stroke_active;
    int       last_brush_cx, last_brush_cy;
    uint8_t   stroke_value;
    int       selecting;
    int       sel_start_x, sel_start_y;
    int       sel_end_x,   sel_end_y;
    Clipboard clipboard;
} AppState;

/* ================================================================
   math helpers
   ================================================================ */

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ================================================================
   file I/O
   ================================================================ */

static uint8_t *load_grid(const char *path, int w, int h) {
    int n = w * h;
    uint8_t *grid = (uint8_t *)calloc(n, 1);
    if (!grid) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return grid; /* new file, all zeros */
    char magic[2];
    uint16_t fw, fh;
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != 'X')
        { fclose(f); return grid; }
    if (fread(&fw, 2, 1, f) != 1 || fread(&fh, 2, 1, f) != 1)
        { fclose(f); return grid; }
    if (fw != (uint16_t)w || fh != (uint16_t)h)
        { fclose(f); return grid; }
    int bytes = (n + 7) / 8;
    uint8_t *bits = (uint8_t *)calloc(bytes, 1);
    if (!bits) { fclose(f); return grid; }
    if (fread(bits, 1, bytes, f) == (size_t)bytes) {
        for (int i = 0; i < n; i++)
            grid[i] = (bits[i / 8] >> (i % 8)) & 1;
    }
    free(bits);
    fclose(f);
    return grid;
}

static void save_grid(const char *path, const uint8_t *grid, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "pxed: cannot write %s\n", path); return; }
    fwrite(HEADER, 1, 2, f);
    uint16_t fw = (uint16_t)w, fh = (uint16_t)h;
    fwrite(&fw, 2, 1, f);
    fwrite(&fh, 2, 1, f);
    int n = w * h, bytes = (n + 7) / 8;
    uint8_t *bits = (uint8_t *)calloc(bytes, 1);
    if (bits) {
        for (int i = 0; i < n; i++)
            if (grid[i]) bits[i / 8] |= (uint8_t)(1 << (i % 8));
        fwrite(bits, 1, bytes, f);
        free(bits);
    }
    fclose(f);
}

/* ================================================================
   clipboard
   ================================================================ */

static void clipboard_copy(Clipboard *clip, const uint8_t *grid, int grid_w, int grid_h, int x, int y, int w, int h) {
    if (clip->data) free(clip->data);
    clip->x = x;
    clip->y = y;
    clip->w = w;
    clip->h = h;
    int n = w * h;
    clip->data = (uint8_t *)calloc(n, 1);
    if (!clip->data) return;
    for (int by = 0; by < h; by++) {
        for (int bx = 0; bx < w; bx++) {
            int gx = x + bx, gy = y + by;
            if (gx >= 0 && gx < grid_w && gy >= 0 && gy < grid_h) {
                clip->data[by * w + bx] = grid[gy * grid_w + gx];
            }
        }
    }
}

static void clipboard_free(Clipboard *clip) {
    if (clip->data) { free(clip->data); clip->data = NULL; }
    clip->w = clip->h = 0;
}

/* ================================================================
   camera
   ================================================================ */

static void update_camera(AppState *s, Vector2 mouse, int ctrl_down) {
    float zoom_delta = GetMouseWheelMove();
    if (ctrl_down && (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)))      zoom_delta += 1.0f;
    if (ctrl_down && (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))) zoom_delta -= 1.0f;

    if (zoom_delta != 0.0f) {
        Vector2 anchor = (zoom_delta == 1.0f || zoom_delta == -1.0f)
            ? (Vector2){ s->win_w * 0.5f, s->win_h * 0.5f } : mouse;
        Vector2 before = GetScreenToWorld2D(anchor, s->cam);
        s->cam.zoom    = clampf(s->cam.zoom + zoom_delta * 0.1f * s->cam.zoom, 0.2f, (float)MAX_SCALE);
        Vector2 after  = GetScreenToWorld2D(anchor, s->cam);
        s->cam.target.x += before.x - after.x;
        s->cam.target.y += before.y - after.y;
    }

    float pan = 600.0f * GetFrameTime() / s->cam.zoom;
    if (IsKeyDown(KEY_LEFT))  s->cam.target.x -= pan;
    if (IsKeyDown(KEY_RIGHT)) s->cam.target.x += pan;
    if (IsKeyDown(KEY_UP))    s->cam.target.y -= pan;
    if (IsKeyDown(KEY_DOWN))  s->cam.target.y += pan;

    if (IsKeyPressed(KEY_R)) {
        s->cam.zoom   = s->initial_zoom;
        s->cam.target = (Vector2){ s->canvas_w * 0.5f, s->canvas_h * 0.5f };
    }

    float hvw = (float)s->win_w / (2.0f * s->cam.zoom);
    float hvh = (float)s->win_h / (2.0f * s->cam.zoom);
    s->cam.target.x = ((float)s->canvas_w > 2.0f * hvw)
        ? clampf(s->cam.target.x, hvw, (float)s->canvas_w - hvw) : s->canvas_w * 0.5f;
    s->cam.target.y = ((float)s->canvas_h > 2.0f * hvh)
        ? clampf(s->cam.target.y, hvh, (float)s->canvas_h - hvh) : s->canvas_h * 0.5f;
}

/* ================================================================
   keyboard input
   ================================================================ */

static void handle_keys(AppState *s, int ctrl_down, int w, int h) {
    if (IsKeyPressed(KEY_G)) s->show_grid = !s->show_grid;

    if (IsKeyPressed(KEY_P)) {
        s->active_tool = TOOL_PENCIL;
        s->selecting   = 0;
        clipboard_free(&s->clipboard);
    }
    if (IsKeyPressed(KEY_E)) {
        s->active_tool = TOOL_ERASER;
        s->selecting   = 0;
        clipboard_free(&s->clipboard);
    }
    if (IsKeyPressed(KEY_S)) {
        s->active_tool = TOOL_SELECT;
        s->selecting   = 0;
        clipboard_free(&s->clipboard);
    }

    if (!ctrl_down) {
        int max_brush = (w < h) ? w : h;
        if ((IsKeyPressed(KEY_EQUAL)  || IsKeyPressed(KEY_KP_ADD))      && s->brush_size < max_brush) s->brush_size++;
        if ((IsKeyPressed(KEY_MINUS)  || IsKeyPressed(KEY_KP_SUBTRACT)) && s->brush_size > 1)         s->brush_size--;
    }
}

/* ================================================================
   mouse input  (selection, drawing, copy/paste)
   ================================================================ */

static void handle_mouse(AppState *s, uint8_t *grid, Vector2 mouse, int w, int h) {
    int cp = s->cell_px;

    if (s->active_tool == TOOL_SELECT) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s->selecting = 0;
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                    s->selecting       = 1;
                    s->sel_start_x     = s->sel_end_x = cx;
                    s->sel_start_y     = s->sel_end_y = cy;
                }
            }
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && s->selecting) {
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                    s->sel_end_x = cx;
                    s->sel_end_y = cy;
                }
            }
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && s->selecting) {
            int sx = (s->sel_start_x < s->sel_end_x) ? s->sel_start_x : s->sel_end_x;
            int sy = (s->sel_start_y < s->sel_end_y) ? s->sel_start_y : s->sel_end_y;
            int sw = abs(s->sel_end_x - s->sel_start_x) + 1;
            int sh = abs(s->sel_end_y - s->sel_start_y) + 1;
            clipboard_copy(&s->clipboard, grid, w, h, sx, sy, sw, sh);
            s->selecting   = 0;
            s->active_tool = TOOL_CLIPBOARD;
        }
    } else if (s->active_tool == TOOL_CLIPBOARD) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && s->clipboard.data) {
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int base_x = (int)(mp.x / cp) - s->clipboard.w / 2;
                int base_y = (int)(mp.y / cp) - s->clipboard.h / 2;
                for (int by = 0; by < s->clipboard.h; by++)
                    for (int bx = 0; bx < s->clipboard.w; bx++) {
                        int gx = base_x + bx, gy = base_y + by;
                        if (gx >= 0 && gx < w && gy >= 0 && gy < h)
                            grid[gy * w + gx] = s->clipboard.data[by * s->clipboard.w + bx] ? 1 : 0;
                    }
            }
        }
    } else {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s->stroke_active = 0;
            s->last_brush_cx = s->last_brush_cy = -1;
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                    s->stroke_value  = (s->active_tool == TOOL_PENCIL) ? 1 : 0;
                    s->stroke_active = 1;
                }
            }
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && s->stroke_active) {
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h
                    && (cx != s->last_brush_cx || cy != s->last_brush_cy)) {
                    int bx0 = cx - s->brush_size / 2;
                    int by0 = cy - s->brush_size / 2;
                    for (int by = 0; by < s->brush_size; by++) {
                        int gy = by0 + by;
                        if (gy < 0 || gy >= h) continue;
                        for (int bx = 0; bx < s->brush_size; bx++) {
                            int gx = bx0 + bx;
                            if (gx >= 0 && gx < w)
                                grid[gy * w + gx] = s->stroke_value;
                        }
                    }
                    s->last_brush_cx = cx;
                    s->last_brush_cy = cy;
                }
            }
        } else if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            s->stroke_active = 0;
            s->last_brush_cx = s->last_brush_cy = -1;
        }
    }

}

/* ================================================================
   rendering
   ================================================================ */

static void draw_frame(const AppState *s, const uint8_t *grid, Vector2 mouse, int w, int h) {
    int cp = s->cell_px;

    BeginDrawing();
    ClearBackground(LIGHTGRAY);
    BeginMode2D(s->cam);

    DrawRectangle(0, 0, s->canvas_w, s->canvas_h, WHITE);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (grid[y * w + x])
                DrawRectangle(x * cp, y * cp, cp, cp, BLACK);

    if (s->show_grid) {
        for (int x = 0; x <= w; x++) DrawLine(x * cp, 0, x * cp, s->canvas_h, LIGHTGRAY);
        for (int y = 0; y <= h; y++) DrawLine(0, y * cp, s->canvas_w, y * cp, LIGHTGRAY);
    }

    /* selection cursor: 1x1 at rest, stretches while dragging */
    if (s->active_tool == TOOL_SELECT) {
        Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
        if (mp.x >= 0 && mp.y >= 0) {
            int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
            if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                int sx = cx, sy = cy, sw = 1, sh = 1;
                if (s->selecting) {
                    sx = (s->sel_start_x < s->sel_end_x) ? s->sel_start_x : s->sel_end_x;
                    sy = (s->sel_start_y < s->sel_end_y) ? s->sel_start_y : s->sel_end_y;
                    sw = abs(s->sel_end_x - s->sel_start_x) + 1;
                    sh = abs(s->sel_end_y - s->sel_start_y) + 1;
                }
                Rectangle sr = { sx * cp, sy * cp, sw * cp, sh * cp };
                DrawRectangleRec(sr,           (Color){  0, 200, 100,  30 });
                DrawRectangleLinesEx(sr, 2.0f, (Color){  0, 200, 100, 200 });
            }
        }
    }

    /* clipboard paste preview (clipboard brush only) */
    if (s->active_tool == TOOL_CLIPBOARD && s->clipboard.data) {
        Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
        int bx0 = (int)(mp.x / cp) - s->clipboard.w / 2;
        int by0 = (int)(mp.y / cp) - s->clipboard.h / 2;
        Rectangle pr = { bx0 * cp, by0 * cp, s->clipboard.w * cp, s->clipboard.h * cp };
        DrawRectangleRec(pr,           (Color){   0, 150, 255,  80 });
        DrawRectangleLinesEx(pr, 2.0f, (Color){   0, 100, 200, 200 });
        for (int by = 0; by < s->clipboard.h; by++)
            for (int bx = 0; bx < s->clipboard.w; bx++)
                if (s->clipboard.data[by * s->clipboard.w + bx]) {
                    int px = bx0 + bx, py = by0 + by;
                    if (px >= 0 && px < w && py >= 0 && py < h)
                        DrawRectangle(px * cp, py * cp, cp, cp, (Color){ 100, 200, 255, 120 });
                }
    }

    /* brush cursor (pencil/eraser only) */
    if (s->active_tool == TOOL_PENCIL || s->active_tool == TOOL_ERASER) {
        Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
        if (mp.x >= 0 && mp.y >= 0) {
            int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
            if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                int bx0 = cx - s->brush_size / 2, by0 = cy - s->brush_size / 2;
                Rectangle br = { bx0 * cp, by0 * cp, s->brush_size * cp, s->brush_size * cp };
                Color ov = (s->active_tool == TOOL_PENCIL)
                    ? (Color){ 0, 0, 0, 90 } : (Color){ 255, 255, 255, 120 };
                DrawRectangleRec(br, ov);
                DrawRectangleLinesEx(br, 2.0f, DARKGRAY);
            }
        }
    }

    EndMode2D();
    EndDrawing();
}

/* ================================================================
   usage / help
   ================================================================ */

static void usage(FILE *out, int code) {
    fprintf(out, "usage: pxed -w <width> -h <height> -f <file> [-s <scale>] [--help]\n\n");
    fprintf(out, "options:\n");
    fprintf(out, "  -w <width>   canvas width in cells (1-%d)\n", MAX_DIM);
    fprintf(out, "  -h <height>  canvas height in cells (1-%d)\n", MAX_DIM);
    fprintf(out, "  -f <file>    path to the .px save file\n");
    fprintf(out, "  -s <scale>   initial window scale multiplier (%.1f-%.1f, fractional allowed)\n", MIN_SCALE, MAX_SCALE);
    fprintf(out, "  --help, -?   show this help and exit\n\n");
    fprintf(out, "keyboard shortcuts:\n");
    fprintf(out, "  P / E / S    select pencil / eraser / selection\n");
    fprintf(out, "  + / -        increase / decrease brush size (pencil/eraser)\n");
    fprintf(out, "  Left Drag    paint / select region\n");
    fprintf(out, "  Release Drag auto-copy selection (switches to clipboard brush)\n");
    fprintf(out, "  Left Click   paste clipboard brush at cursor\n");
    fprintf(out, "  Mouse Wheel  zoom camera\n");
    fprintf(out, "  Ctrl + / -   zoom in/out\n");
    fprintf(out, "  Arrow Keys   pan camera\n");
    fprintf(out, "  R            reset camera\n");
    fprintf(out, "  G            toggle grid\n");
    exit(code);
}

/* ================================================================
   entry point
   ================================================================ */

int main(int argc, char **argv) {
    int w = 0, h = 0;
    float scale = 1.0f;
    const char *fpath = NULL;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-?")) { usage(stdout, 0); }
        if      (!strcmp(argv[i], "-w") && i+1 < argc) { w = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-h") && i+1 < argc) { h = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-f") && i+1 < argc) { fpath = argv[++i]; }
        else if (!strcmp(argv[i], "-s") && i+1 < argc) {
            char *end = NULL;
            scale = strtof(argv[++i], &end);
            if (!end || *end != '\0') usage(stderr, 1);
        }
        else usage(stderr, 1);
    }
    if (w < MIN_DIM || w > MAX_DIM) { fprintf(stderr, "pxed: width must be 1-%d\n",  MAX_DIM); return 1; }
    if (h < MIN_DIM || h > MAX_DIM) { fprintf(stderr, "pxed: height must be 1-%d\n", MAX_DIM); return 1; }
    if (!fpath)                      { fprintf(stderr, "pxed: -f <file> required\n");            return 1; }
    if (scale < MIN_SCALE || scale > MAX_SCALE) {
        fprintf(stderr, "pxed: scale must be %.1f-%.1f\n", MIN_SCALE, MAX_SCALE);
        return 1;
    }

    uint8_t *grid = load_grid(fpath, w, h);
    if (!grid) { fprintf(stderr, "pxed: out of memory\n"); return 1; }

    AppState s     = {0};
    s.cell_px      = CELL_PX;
    s.canvas_w     = w * CELL_PX;
    s.canvas_h     = h * CELL_PX;
    s.win_w        = (int)(s.canvas_w * scale);
    s.win_h        = (int)(s.canvas_h * scale);
    if (s.win_w < 1) s.win_w = 1;
    if (s.win_h < 1) s.win_h = 1;

    float fit_zoom_x = (float)s.win_w / (float)s.canvas_w;
    float fit_zoom_y = (float)s.win_h / (float)s.canvas_h;
    float fit_zoom   = (fit_zoom_x < fit_zoom_y) ? fit_zoom_x : fit_zoom_y;
    s.cam.offset    = (Vector2){ s.win_w * 0.5f, s.win_h * 0.5f };
    s.cam.target    = (Vector2){ s.canvas_w * 0.5f, s.canvas_h * 0.5f };
    s.cam.zoom      = fit_zoom;
    s.initial_zoom  = fit_zoom;
    s.brush_size    = 1;
    s.show_grid     = 1;
    s.active_tool   = TOOL_PENCIL;
    s.last_brush_cx = -1;
    s.last_brush_cy = -1;

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(s.win_w, s.win_h, "pxed");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        Vector2 mouse     = GetMousePosition();
        int     ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        update_camera(&s, mouse, ctrl_down);
        handle_keys(&s, ctrl_down, w, h);
        handle_mouse(&s, grid, mouse, w, h);
        draw_frame(&s, grid, mouse, w, h);
    }

    CloseWindow();
    clipboard_free(&s.clipboard);
    save_grid(fpath, grid, w, h);
    free(grid);
    return 0;
}
