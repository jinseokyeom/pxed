# pxed
simple pixel editor for desktop

## Build

Requires [raylib](https://www.raylib.com) 5.x.

```sh
# if raylib is installed system-wide / found by pkg-config
make

# or point to a local raylib build/download
make RAYLIB_PATH=/path/to/raylib
```

## Usage

```sh
pxed -w <width> -h <height> -f <file> [-s <scale>]
```

| flag | description |
|------|-------------|
| `-w` | canvas width in cells (1–1024) |
| `-h` | canvas height in cells (1–1024) |
| `-f` | path to the `.px` save file (created if it does not exist) |
| `-s` | optional initial window scale multiplier (0.1–64, default: 1; fractional values allowed) |
| `--help` | print command usage and keyboard shortcuts |

**Example**

```sh
pxed -w 160 -h 160 -f "my_pixel_art.px" -s 2
```

- Press **P** for Pencil, **E** for Eraser, and **S** for Selection.

### Keyboard shortcuts

| key | action |
|-----|--------|
| `P` | select Pencil tool (clears clipboard brush) |
| `E` | select Eraser tool (clears clipboard brush) |
| `S` | select Selection tool (clears clipboard brush) |
| `+` / `-` | increase / decrease brush size (pencil/eraser) |
| Left drag (P/E) | paint continuously |
| Left drag (S) | define selection rectangle (starts at 1x1, stretches while dragging) |
| Left release (S) | auto-copy selection and switch to Clipboard Brush |
| Left click (Clipboard Brush) | paste/stamp clipboard at cursor |
| Mouse wheel | zoom camera |
| Ctrl `+` / Ctrl `-` | zoom camera in/out |
| Arrow keys | pan camera |
| `R` | reset camera zoom/position |
| `G` | toggle grid visibility |

- Clipboard Brush preview is centered on the cursor.
- Drawing is auto-saved when the window is closed.

## File format

Files are compact binary with the following layout:

| offset | size | description |
|--------|------|-------------|
| 0      | 2 B  | magic `PX`  |
| 2      | 2 B  | width (uint16, little-endian) |
| 4      | 2 B  | height (uint16, little-endian) |
| 6      | ⌈w×h/8⌉ B | pixel bits, row-major, LSB first |
