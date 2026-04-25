# pxed
simple pixel editor for desktop

## Quick Start

### Build

Requires [raylib](https://www.raylib.com) 5.x.

```sh
# if raylib is installed system-wide / found by pkg-config
make

# or point to a local raylib build/download
make RAYLIB_PATH=/path/to/raylib
```

### Launch

```sh
pxed -w <width> -h <height> -f <file> [-s <scale>]
```

**Example:** `pxed -w 160 -h 160 -f "my_pixel_art.px" -s 2`

For help, run: `pxed --help`

---

## Manual

### Command-line Arguments

| flag | description |
|------|-------------|
| `-w` | canvas width in cells (1–1024) |
| `-h` | canvas height in cells (1–1024) |
| `-f` | path to the `.px` save file (created if it does not exist) |
| `-s` | optional initial window scale multiplier (0.1–64, default: 1; fractional values allowed) |
| `--help` | print command usage and keyboard shortcuts |

### Getting Started

1. **Create a new project:** `pxed -w 16 -h 16 -f myart.px`
2. **Select a tool:** Press **P** (Pencil), **E** (Eraser), **S** (Selection), **L** (Line), or **F** (Fill)
3. **Draw:** Use your mouse to create pixel art
4. **Save:** Close the window—your work is automatically saved to the file
5. **Reopen:** `pxed -w 16 -h 16 -f myart.px` to continue editing

### Tools

#### Pencil (`P`)
Paint filled cells with the current brush size. Use `+` and `-` to adjust brush size.
- **Left drag:** Paint continuously
- **Brush size:** Adjust with `+` (larger) and `-` (smaller) keys

#### Eraser (`E`)
Clear (remove) filled cells with the current brush size. Operates like the Pencil but erases instead.
- **Left drag:** Erase continuously
- **Brush size:** Adjust with `+` (larger) and `-` (smaller) keys

#### Selection (`S`)
Copy regions of your artwork to use as a reusable brush.
1. **Left drag:** Draw a rectangular selection box (starts at 1×1, expands as you drag)
2. **Left release:** The selection is auto-copied; tool switches to Clipboard Brush
3. **Left click (Clipboard Brush):** Paste/stamp the copied artwork at the cursor position
- **Ctrl + `0`-`9`:** Store the current clipboard patch to a slot
- **Number keys `0`-`9`:** Recall a stored slot into Clipboard Brush
- **Clipboard Brush preview:** Centered on the cursor, showing semi-transparent preview

#### Line (`L`)
Draw straight lines with two clicks.
1. **First left click:** Set the line start point
2. **Second left click:** Complete the line (preview shown while dragging between clicks)

#### Fill (`F`)
Flood-fill the contiguous region of cells under the cursor. Fills all connected cells of the same type.
- **Left click:** Flood fill from cursor position

### Navigation & View

| key | action |
|-----|--------|
| **Mouse wheel** | zoom camera in/out |
| **Ctrl `+` / Ctrl `-`** | zoom camera in/out (alternative) |
| **Arrow keys** | pan camera (move view) |
| **`R`** | reset camera zoom and position to default |
| **`G`** | toggle grid visibility (helpful for precision) |

### Editing & History

| key | action |
|-----|--------|
| **Ctrl `Z`** | undo (up to 64 steps) |
| **Ctrl `Y`** | redo |

### Complete Keyboard Reference

| key | action |
|-----|--------|
| `P` | select Pencil tool (clears clipboard brush) |
| `E` | select Eraser tool (clears clipboard brush) |
| `S` | select Selection tool (clears clipboard brush) |
| `L` | select Line tool (clears clipboard brush) |
| `F` | select Fill tool (clears clipboard brush) |
| Ctrl `0`-`9` | save the current clipboard patch to that slot |
| `0`-`9` | switch to Clipboard Brush with that stored slot |
| `+` / `-` | increase / decrease brush size (pencil/eraser) |
| Mouse wheel | zoom camera |
| Ctrl `+` / Ctrl `-` | zoom camera in/out |
| Ctrl `Z` / Ctrl `Y` | undo / redo |
| Arrow keys | pan camera |
| `R` | reset camera zoom/position |
| `G` | toggle grid visibility |

### Tips & Tricks

- **Use the grid (`G`)** when precision is needed for alignment
- **Zoom in (`Ctrl +`)** for detailed work on individual pixels
- **Copy and paste** by selecting an area, then stamping it multiple times
- **Undo frequently** with Ctrl `Z` — up to 64 steps are available
- **Auto-save** happens when you close the window, so your work is always preserved

### File Format

Files are compact binary with the following layout:

| offset | size | description |
|--------|------|-------------|
| 0      | 2 B  | magic `PX`  |
| 2      | 2 B  | width (uint16, little-endian) |
| 4      | 2 B  | height (uint16, little-endian) |
| 6      | ⌈w×h/8⌉ B | pixel bits, row-major, LSB first |

### Troubleshooting

**Q: Where are my files saved?**  
A: Files are saved to the path you specify with the `-f` flag. They are automatically saved when you close the editor.

**Q: Can I undo my mistakes?**  
A: Yes! Use Ctrl `Z` to undo (up to 64 steps) and Ctrl `Y` to redo.

**Q: How do I change the canvas size?**  
A: Canvas size is set when launching. Create a new file with different dimensions if needed.

**Q: Can I export to other formats?**  
A: Currently, pxed saves to its native `.px` format. You can extend the source code to add export functionality.

## File format

Files are compact binary with the following layout:

| offset | size | description |
|--------|------|-------------|
| 0      | 2 B  | magic `PX`  |
| 2      | 2 B  | width (uint16, little-endian) |
| 4      | 2 B  | height (uint16, little-endian) |
| 6      | ⌈w×h/8⌉ B | pixel bits, row-major, LSB first |
