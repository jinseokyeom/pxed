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
pxed -w <width> -h <height> -f <file>
```

| flag | description |
|------|-------------|
| `-w` | canvas width in cells (1–1024) |
| `-h` | canvas height in cells (1–1024) |
| `-f` | path to the `.px` save file (created if it does not exist) |

**Example**

```sh
pxed -w 160 -h 160 -f "my_pixel_art.px"
```

- **Left-click** a cell to flip its value (black ↔ white).
- The drawing is **auto-saved** when the window is closed.

## File format

Files are compact binary with the following layout:

| offset | size | description |
|--------|------|-------------|
| 0      | 2 B  | magic `PX`  |
| 2      | 2 B  | width (uint16, little-endian) |
| 4      | 2 B  | height (uint16, little-endian) |
| 6      | ⌈w×h/8⌉ B | pixel bits, row-major, LSB first |
