/* pxed - simple pixel editor
 * usage: pxed -w <width> -h <height> -f <file>
 * click a cell to flip its binary value (black/white)
 * auto-saves on close in compact binary format
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"

#define CELL_PX   6     /* pixels per cell on screen */
#define MIN_DIM   1
#define MAX_DIM   1024
#define HEADER    "PX"  /* 2-byte magic */

/* ---- file I/O ---- */
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
        { fclose(f); return grid; } /* dimension mismatch, start fresh */
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

/* ---- arg parsing ---- */
static void usage(void) {
    fprintf(stderr, "usage: pxed -w <width> -h <height> -f <file>\n");
    exit(1);
}

int main(int argc, char **argv) {
    int w = 0, h = 0;
    const char *fpath = NULL;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-w") && i+1 < argc) { w = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-h") && i+1 < argc) { h = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "-f") && i+1 < argc) { fpath = argv[++i]; }
        else usage();
    }
    if (w < MIN_DIM || w > MAX_DIM) { fprintf(stderr, "pxed: width must be 1-%d\n", MAX_DIM); return 1; }
    if (h < MIN_DIM || h > MAX_DIM) { fprintf(stderr, "pxed: height must be 1-%d\n", MAX_DIM); return 1; }
    if (!fpath) { fprintf(stderr, "pxed: -f <file> required\n"); return 1; }

    uint8_t *grid = load_grid(fpath, w, h);
    if (!grid) { fprintf(stderr, "pxed: out of memory\n"); return 1; }

    int win_w = w * CELL_PX;
    int win_h = h * CELL_PX;

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(win_w, win_h, "pxed");
    SetTargetFPS(60);

    while (!WindowShouldClose()) {
        /* handle mouse click - flip cell once per press */
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mp = GetMousePosition();
            if (mp.x >= 0 && mp.y >= 0) {
                int cx = (int)(mp.x / CELL_PX);
                int cy = (int)(mp.y / CELL_PX);
                if (cx >= 0 && cx < w && cy >= 0 && cy < h)
                    grid[cy * w + cx] ^= 1;
            }
        }

        BeginDrawing();
        ClearBackground(WHITE);

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (grid[y * w + x])
                    DrawRectangle(x * CELL_PX, y * CELL_PX, CELL_PX, CELL_PX, BLACK);
            }
        }

        /* draw grid lines */
        for (int x = 0; x <= w; x++)
            DrawLine(x * CELL_PX, 0, x * CELL_PX, win_h, LIGHTGRAY);
        for (int y = 0; y <= h; y++)
            DrawLine(0, y * CELL_PX, win_w, y * CELL_PX, LIGHTGRAY);

        EndDrawing();
    }

    CloseWindow();
    save_grid(fpath, grid, w, h);
    free(grid);
    return 0;
}
