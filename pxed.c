#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"

#define CELL_PX         6
#define MIN_DIM         1
#define MAX_DIM         1024
#define MIN_SCALE       0.1f
#define MAX_SCALE       64.0f
#define HEADER          "PX"
#define HISTORY_LIMIT   64

#define SWITCH_TOOL(s, t) do {          \
    (s)->active_tool  = (t);            \
    (s)->line_pending = 0;              \
    (s)->rect_pending = 0;              \
    (s)->selecting    = 0;              \
    clipboard_free(&(s)->clipboard);    \
} while (0)

typedef enum Tool {
    TOOL_PENCIL    = 0,
    TOOL_ERASER    = 1,
    TOOL_SELECT    = 2,
    TOOL_CLIPBOARD = 3,
    TOOL_LINE      = 4,
    TOOL_FILL      = 5,
    TOOL_RECT      = 6
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
    int       line_pending;
    int       line_start_x, line_start_y;
    int       rect_pending;
    int       rect_start_x, rect_start_y;
    int       selecting;
    int       sel_start_x, sel_start_y;
    int       sel_end_x,   sel_end_y;
    Clipboard clipboard;
} AppState;

typedef struct {
    uint8_t *undo[HISTORY_LIMIT];
    uint8_t *redo[HISTORY_LIMIT];
    int      undo_count;
    int      redo_count;
    int      n;
} History;

static inline float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline void tool_overlay_colors(Tool tool, Color *fill, Color *outline) {
    switch (tool) {
        case TOOL_PENCIL:    *fill = (Color){ 32,  32,  32,  95 }; *outline = (Color){ 44,  44,  44, 220 }; break;
        case TOOL_ERASER:    *fill = (Color){255, 255, 255, 140 }; *outline = (Color){150, 150, 150, 230 }; break;
        case TOOL_SELECT:    *fill = (Color){  0, 200, 100,  35 }; *outline = (Color){  0, 180,  90, 220 }; break;
        case TOOL_CLIPBOARD: *fill = (Color){  0, 150, 255,  80 }; *outline = (Color){  0, 100, 210, 220 }; break;
        case TOOL_LINE:      *fill = (Color){230,  70,  70, 120 }; *outline = (Color){180,  40,  40, 230 }; break;
        case TOOL_FILL:      *fill = (Color){165,  85, 220, 120 }; *outline = (Color){120,  55, 175, 230 }; break;
        case TOOL_RECT:      *fill = (Color){255, 220,  70, 120 }; *outline = (Color){200, 160,  30, 230 }; break;
    }
}

static void raster_line_to_grid(uint8_t *grid, int w, int h, int x0, int y0, int x1, int y1, uint8_t value) {
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h) {
            grid[y0 * w + x0] = value;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void raster_line_overlay(int w, int h, int cp, int x0, int y0, int x1, int y1, Color fill) {
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (x0 >= 0 && x0 < w && y0 >= 0 && y0 < h)
            DrawRectangle(x0 * cp, y0 * cp, cp, cp, fill);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void raster_rect_to_grid(uint8_t *grid, int w, int h, int x0, int y0, int x1, int y1, uint8_t value) {
    int left = (x0 < x1) ? x0 : x1;
    int right = (x0 > x1) ? x0 : x1;
    int top = (y0 < y1) ? y0 : y1;
    int bottom = (y0 > y1) ? y0 : y1;

    for (int x = left; x <= right; x++) {
        if (x >= 0 && x < w) {
            if (top >= 0 && top < h) grid[top * w + x] = value;
            if (bottom >= 0 && bottom < h) grid[bottom * w + x] = value;
        }
    }
    for (int y = top; y <= bottom; y++) {
        if (y >= 0 && y < h) {
            if (left >= 0 && left < w) grid[y * w + left] = value;
            if (right >= 0 && right < w) grid[y * w + right] = value;
        }
    }
}

static void flood_fill(uint8_t *grid, int w, int h, int sx, int sy, uint8_t new_value) {
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) return;

    uint8_t old_value = grid[sy * w + sx];
    if (old_value == new_value) return;

    int max = w * h;
    int *stack = (int *)malloc((size_t)max * sizeof(int));
    if (!stack) {
        return;
    }

    int top = 0;
    int start = sy * w + sx;
    stack[top++] = start;
    grid[start] = new_value;

    while (top > 0) {
        int idx = stack[--top];
        int x = idx % w;
        int y = idx / w;

        if (x + 1 < w) {
            int right = idx + 1;
            if (grid[right] == old_value) {
                grid[right] = new_value;
                stack[top++] = right;
            }
        }
        if (x > 0) {
            int left = idx - 1;
            if (grid[left] == old_value) {
                grid[left] = new_value;
                stack[top++] = left;
            }
        }
        if (y + 1 < h) {
            int down = idx + w;
            if (grid[down] == old_value) {
                grid[down] = new_value;
                stack[top++] = down;
            }
        }
        if (y > 0) {
            int up = idx - w;
            if (grid[up] == old_value) {
                grid[up] = new_value;
                stack[top++] = up;
            }
        }
    }

    free(stack);
}

static void flood_fill_overlay(const uint8_t *grid, int w, int h, int sx, int sy, int cp, Color fill) {
    if (sx < 0 || sx >= w || sy < 0 || sy >= h) return;

    uint8_t old_value = grid[sy * w + sx];
    if (old_value == 1) return; /* fill tool writes 1, so preview only real changes */

    int max = w * h;
    int *stack = (int *)malloc((size_t)max * sizeof(int));
    uint8_t *seen = (uint8_t *)calloc((size_t)max, 1);
    if (!stack || !seen) {
        free(stack);
        free(seen);
        return;
    }

    int top = 0;
    int start = sy * w + sx;
    stack[top++] = start;
    seen[start] = 1;

    while (top > 0) {
        int idx = stack[--top];
        if (grid[idx] != old_value) continue;

        int x = idx % w;
        int y = idx / w;

        DrawRectangle(x * cp, y * cp, cp, cp, fill);

        if (x + 1 < w) {
            int right = idx + 1;
            if (!seen[right]) {
                seen[right] = 1;
                stack[top++] = right;
            }
        }
        if (x > 0) {
            int left = idx - 1;
            if (!seen[left]) {
                seen[left] = 1;
                stack[top++] = left;
            }
        }
        if (y + 1 < h) {
            int down = idx + w;
            if (!seen[down]) {
                seen[down] = 1;
                stack[top++] = down;
            }
        }
        if (y > 0) {
            int up = idx - w;
            if (!seen[up]) {
                seen[up] = 1;
                stack[top++] = up;
            }
        }
    }

    free(stack);
    free(seen);
}

static void history_clear_stack(uint8_t **stack, int *count) {
    for (int i = 0; i < *count; i++) {
        free(stack[i]);
        stack[i] = NULL;
    }
    *count = 0;
}

static int history_push_snapshot(uint8_t **stack, int *count, int n, const uint8_t *grid) {
    uint8_t *snap = (uint8_t *)malloc((size_t)n);
    if (!snap) return 0;
    memcpy(snap, grid, (size_t)n);

    if (*count == HISTORY_LIMIT) {
        free(stack[0]);
        memmove(&stack[0], &stack[1], (size_t)(HISTORY_LIMIT - 1) * sizeof(uint8_t *));
        *count = HISTORY_LIMIT - 1;
    }
    stack[*count] = snap;
    (*count)++;
    return 1;
}

static inline void history_init(History *h, int n) {
    memset(h, 0, sizeof(*h));
    h->n = n;
}

static inline void history_free(History *h) {
    history_clear_stack(h->undo, &h->undo_count);
    history_clear_stack(h->redo, &h->redo_count);
}

static inline void history_record(History *h, const uint8_t *grid) {
    if (!history_push_snapshot(h->undo, &h->undo_count, h->n, grid)) return;
    history_clear_stack(h->redo, &h->redo_count);
}

static void history_undo(History *h, uint8_t *grid) {
    if (h->undo_count == 0) return;
    if (!history_push_snapshot(h->redo, &h->redo_count, h->n, grid)) return;
    h->undo_count--;
    memcpy(grid, h->undo[h->undo_count], (size_t)h->n);
    free(h->undo[h->undo_count]);
    h->undo[h->undo_count] = NULL;
}

static void history_redo(History *h, uint8_t *grid) {
    if (h->redo_count == 0) return;
    if (!history_push_snapshot(h->undo, &h->undo_count, h->n, grid)) return;
    h->redo_count--;
    memcpy(grid, h->redo[h->redo_count], (size_t)h->n);
    free(h->redo[h->redo_count]);
    h->redo[h->redo_count] = NULL;
}

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

static inline void clipboard_free(Clipboard *clip) {
    if (clip->data) { free(clip->data); clip->data = NULL; }
    clip->w = clip->h = 0;
}

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

    if (ctrl_down && IsKeyPressed(KEY_R)) {
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

static void handle_keys(AppState *s, int ctrl_down, int w, int h, uint8_t *grid, History *hist) {
    int max_brush = (w < h) ? w : h;
    int key;
    while ((key = GetKeyPressed()) != KEY_NULL) {
        switch (key) {
            case KEY_G:
                s->show_grid = !s->show_grid;
                break;
            case KEY_Z:
                if (ctrl_down) {
                    history_undo(hist, grid);
                    s->line_pending = 0;
                    s->rect_pending = 0;
                    s->selecting    = 0;
                }
                break;
            case KEY_Y:
                if (ctrl_down) {
                    history_redo(hist, grid);
                    s->line_pending = 0;
                    s->rect_pending = 0;
                    s->selecting    = 0;
                }
                break;
            case KEY_P: SWITCH_TOOL(s, TOOL_PENCIL); break;
            case KEY_E: SWITCH_TOOL(s, TOOL_ERASER); break;
            case KEY_S: SWITCH_TOOL(s, TOOL_SELECT); break;
            case KEY_L: SWITCH_TOOL(s, TOOL_LINE);   break;
            case KEY_F: SWITCH_TOOL(s, TOOL_FILL);   break;
            case KEY_R:
                if (!ctrl_down) SWITCH_TOOL(s, TOOL_RECT);
                break;
            case KEY_EQUAL:
            case KEY_KP_ADD:
                if (!ctrl_down && s->brush_size < max_brush) s->brush_size++;
                break;
            case KEY_MINUS:
            case KEY_KP_SUBTRACT:
                if (!ctrl_down && s->brush_size > 1) s->brush_size--;
                break;
            default: break;
        }
    }
}

static void handle_mouse(AppState *s, uint8_t *grid, Vector2 mouse, int w, int h, History *hist) {
    int cp = s->cell_px;

    switch (s->active_tool) {
        case TOOL_SELECT:
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
            break;
        case TOOL_CLIPBOARD:
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && s->clipboard.data) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    history_record(hist, grid);
                    int base_x = (int)(mp.x / cp) - s->clipboard.w / 2;
                    int base_y = (int)(mp.y / cp) - s->clipboard.h / 2;
                    for (int by = 0; by < s->clipboard.h; by++)
                        for (int bx = 0; bx < s->clipboard.w; bx++) {
                            int gx = base_x + bx, gy = base_y + by;
                            if (gx >= 0 && gx < w && gy >= 0 && gy < h
                                && s->clipboard.data[by * s->clipboard.w + bx])
                                grid[gy * w + gx] = 1;
                        }
                }
            }
            break;
        case TOOL_LINE:
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                s->line_pending = 0;
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        s->line_start_x = cx;
                        s->line_start_y = cy;
                        s->line_pending = 1;
                    }
                }
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && s->line_pending) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        history_record(hist, grid);
                        raster_line_to_grid(grid, w, h, s->line_start_x, s->line_start_y, cx, cy, 1);
                    }
                }
                s->line_pending = 0;
            }
            break;
        case TOOL_RECT:
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                s->rect_pending = 0;
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        s->rect_start_x = cx;
                        s->rect_start_y = cy;
                        s->rect_pending = 1;
                    }
                }
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && s->rect_pending) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        history_record(hist, grid);
                        raster_rect_to_grid(grid, w, h, s->rect_start_x, s->rect_start_y, cx, cy, 1);
                    }
                }
                s->rect_pending = 0;
            }
            break;
        case TOOL_FILL:
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        if (grid[cy * w + cx] != 1) {
                            history_record(hist, grid);
                            flood_fill(grid, w, h, cx, cy, 1);
                        }
                    }
                }
            }
            break;
        default:
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                s->stroke_active = 0;
                s->last_brush_cx = s->last_brush_cy = -1;
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        s->stroke_value  = (s->active_tool == TOOL_PENCIL) ? 1 : 0;
                        s->stroke_active = 1;
                        history_record(hist, grid);
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
            break;
    }

}

static void draw_frame(const AppState *s, const uint8_t *grid, Vector2 mouse, int w, int h) {
    int cp = s->cell_px;
    Color tool_fill;
    Color tool_outline;
    Color checker_light = (Color){ 248, 248, 248, 255 };
    Color checker_dark  = (Color){ 232, 232, 232, 255 };
    tool_overlay_colors(s->active_tool, &tool_fill, &tool_outline);

    BeginDrawing();
    ClearBackground(LIGHTGRAY);
    BeginMode2D(s->cam);

    DrawRectangle(0, 0, s->canvas_w, s->canvas_h, WHITE);

    if (s->show_grid) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                Color tile = ((x + y) & 1) ? checker_dark : checker_light;
                DrawRectangle(x * cp, y * cp, cp, cp, tile);
            }
        }
    }

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (grid[y * w + x])
                DrawRectangle(x * cp, y * cp, cp, cp, (Color){ 32, 32, 32, 255 });

    /* per-tool overlays */
    switch (s->active_tool) {
        case TOOL_SELECT: {
            /* selection cursor: 1x1 at rest, stretches while dragging */
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
                    DrawRectangleRec(sr, tool_fill);
                    DrawRectangleLinesEx(sr, 2.0f, tool_outline);
                }
            }
            break;
        }
        case TOOL_CLIPBOARD:
            /* clipboard paste preview */
            if (s->clipboard.data) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                int bx0 = (int)(mp.x / cp) - s->clipboard.w / 2;
                int by0 = (int)(mp.y / cp) - s->clipboard.h / 2;
                Rectangle pr = { bx0 * cp, by0 * cp, s->clipboard.w * cp, s->clipboard.h * cp };
                DrawRectangleRec(pr, tool_fill);
                DrawRectangleLinesEx(pr, 2.0f, tool_outline);
                for (int by = 0; by < s->clipboard.h; by++)
                    for (int bx = 0; bx < s->clipboard.w; bx++)
                        if (s->clipboard.data[by * s->clipboard.w + bx]) {
                            int px = bx0 + bx, py = by0 + by;
                            if (px >= 0 && px < w && py >= 0 && py < h) {
                                DrawRectangle(px * cp, py * cp, cp, cp, tool_fill);
                            }
                        }
            }
            break;
        case TOOL_LINE:
            /* line preview from first click to current cursor */
            if (s->line_pending) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h)
                        raster_line_overlay(w, h, cp, s->line_start_x, s->line_start_y, cx, cy, tool_fill);
                }
            }
            break;
        case TOOL_FILL: {
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h)
                    flood_fill_overlay(grid, w, h, cx, cy, cp, tool_fill);
            }
            break;
        }
        case TOOL_RECT:
            /* rectangle preview from first click to current cursor */
            if (s->rect_pending) {
                Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
                if (mp.x >= 0 && mp.y >= 0) {
                    int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                    if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                        int sx = (s->rect_start_x < cx) ? s->rect_start_x : cx;
                        int sy = (s->rect_start_y < cy) ? s->rect_start_y : cy;
                        int sw = abs(cx - s->rect_start_x) + 1;
                        int sh = abs(cy - s->rect_start_y) + 1;
                        Rectangle rr = { sx * cp, sy * cp, sw * cp, sh * cp };
                        DrawRectangleRec(rr, tool_fill);
                        DrawRectangleLinesEx(rr, 2.0f, tool_outline);
                    }
                }
            }
            break;
        default: break;
    }

    /* brush cursor (pencil/eraser/line/fill/rect) */
    switch (s->active_tool) {
        case TOOL_PENCIL:
        case TOOL_ERASER:
        case TOOL_LINE:
        case TOOL_FILL:
        case TOOL_RECT: {
            Vector2 mp = GetScreenToWorld2D(mouse, s->cam);
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / cp), cy = (int)(mp.y / cp);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h) {
                    int bsz = (s->active_tool == TOOL_LINE || s->active_tool == TOOL_FILL || s->active_tool == TOOL_RECT)
                        ? 1 : s->brush_size;
                    int bx0 = cx - bsz / 2;
                    int by0 = cy - bsz / 2;
                    Rectangle br = { bx0 * cp, by0 * cp, bsz * cp, bsz * cp };
                    DrawRectangleRec(br, tool_fill);
                    DrawRectangleLinesEx(br, 2.0f, tool_outline);
                }
            }
            break;
        }
        default: break;
    }

    EndMode2D();

    EndDrawing();
}

static void usage(FILE *out, int code) {
    fprintf(out, "usage: pxed -w <width> -h <height> -f <file> [-s <scale>] [--help]\n\n");
    fprintf(out, "options:\n");
    fprintf(out, "  -w <width>   canvas width in cells (1-%d)\n", MAX_DIM);
    fprintf(out, "  -h <height>  canvas height in cells (1-%d)\n", MAX_DIM);
    fprintf(out, "  -f <file>    path to the .px save file\n");
    fprintf(out, "  -s <scale>   initial window scale multiplier (%.1f-%.1f, fractional allowed)\n", MIN_SCALE, MAX_SCALE);
    fprintf(out, "  --help, -?   show this help and exit\n\n");
    fprintf(out, "keyboard shortcuts:\n");
    fprintf(out, "  P / E / S / L / F / R\n");
    fprintf(out, "               pencil / eraser / selection / line / fill / rect\n");
    fprintf(out, "  + / -        increase / decrease brush size (pencil/eraser)\n");
    fprintf(out, "  G            toggle checkerboard background\n");
    fprintf(out, "  Mouse Wheel  zoom camera\n");
    fprintf(out, "  Ctrl + / -   zoom in/out\n");
    fprintf(out, "  Arrow Keys   pan camera\n");
    fprintf(out, "  Ctrl + R     reset camera\n");
    fprintf(out, "  Ctrl + Z / Y undo / redo\n");
    exit(code);
}

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

    int n = w * h;
    History hist;
    history_init(&hist, n);

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
    HideCursor();
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        Vector2 mouse     = GetMousePosition();
        int     ctrl_down = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        update_camera(&s, mouse, ctrl_down);
        handle_keys(&s, ctrl_down, w, h, grid, &hist);
        handle_mouse(&s, grid, mouse, w, h, &hist);
        draw_frame(&s, grid, mouse, w, h);
    }

    CloseWindow();
    clipboard_free(&s.clipboard);
    history_free(&hist);
    save_grid(fpath, grid, w, h);
    free(grid);
    return 0;
}
