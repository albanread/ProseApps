# Sisong 3.0: the text engine

Sisong 2.16 keeps a document as a doubly linked list of line objects, each a
gap buffer of bytes. A column is a byte, so the editor is ASCII-only on a
system that is UTF-8 throughout. Sisong 3.0 replaces the storage and keeps
the editor.

Decided with the owner on 2026-09-20:

| Question | Decision |
|---|---|
| The unit of text in memory | 32-bit Unicode code points; UTF-8 only at the edges (files, clipboard, drawing, keys) |
| The storage | a rope; staged behind an interface, not in one step |
| Undo | snapshots of the rope, once the rope is in |
| Cell widths and undecodable bytes | both in the first release |
| The name | Sisong 3.0 for Prose |

## What a code point is not

- **Not a column.** CJK and emoji take two cells of the grid, combining marks
  and other zero-width characters take none, a tab runs to the next stop.
  Sisong already separates the character column from the screen column for
  tabs (`CharCoordToScreenCoord`, `ScreenCoordToCharCoord`); that pair of
  functions is the one place that learns about widths. The width of a code
  point comes from a table generated from the Unicode data
  (`prose/tools/gen_width_table.py`), compiled in: the same answer on the
  target, in the host's tests, and whatever ICU data an image carries.
- **Not always decodable.** A byte that is not valid UTF-8 becomes the lone
  surrogate U+DC80 + (byte - 0x80), as Python's `surrogateescape` does, is
  drawn as a small hex box, and is written back as the byte it was. A
  Latin-1 or partly binary file opened and saved is byte for byte the same.
- **Not a grapheme.** The cursor steps by code point in 3.0. Treating a base
  letter with its combining marks as one step is a later refinement.

A document also remembers its line ending (LF or CRLF, by majority on load)
and whether it began with a byte order mark, and writes both back. 2.16
silently converted CRLF to LF.

## Layers

```
 editor (edit*.cpp, selection, undo, lexer, painter, find ...)
   |  speaks (line, column) in code points, through DocLine handles
 EditView / Document      per-line side table: lexer exit state, colour
   |                      points, pixel width (a gap buffer of records)
 TextStore                the interface: pure C++, no Be API, host-testable
   |-- LineStore          lines as arrays of code points (stage 2 storage)
   `-- RopeStore          B-tree rope, copy-on-write, snapshots (stage 3)
 utf8 codec, width table  src/text/
```

`TextStore` addresses text by line and column, because that is how the
editor thinks and both stores answer it cheaply:

- `LineCount`, `LineLength(y)`, `CharAt(y, x)` (the newline is the character
  at `x == LineLength(y)`), `GetLine`, `GetText(y, x, count)`;
- `Insert(y, x, text, n)` where the text may hold newlines, and
  `Delete(y, x, count)` where a newline counts as one character. These two
  are all the editor's five editing primitives need;
- `Snapshot()` / `Restore()`: an immutable copy. For the rope it costs
  nothing (nodes are shared and reference counted); for the line store it is
  a deep copy, which only the tests use.

The rope is a B-tree, not a binary concatenation tree: wide fanout, leaves of
up to 1024 code points, every node carrying the number of code points and of
newlines below it, so line to offset and offset to line are both
logarithmic. Nodes are immutable once shared; an edit copies the path from
the root to the leaf it touches.

The cached line pointers of 2.16 (`curline`, `scroll.topline`, the `line`
member of `DocPoint`) were caches of a line number, kept valid by hand
through every insertion and deletion. They go: a `DocLine` is a (document,
line number) pair with the old line object's methods.

## How it is tested

1. **The stores, on the host.** `make check` builds `src/text` with the
   Mac's compiler: unit tests of the codec (every byte sequence class, the
   escapes, round trips of random bytes) and of the width table, then a fuzz
   run that applies the same random edits to a plain model, the line store
   and the rope, comparing all three after every step, snapshots included.
2. **The editor, against its own past.** `Sisong -selftest script out` runs
   a script of keys, selections, clipboard and undo steps against the real
   editor in the real window, and dumps the document and cursor after each
   step. Scripts are generated at random (seeded) on the host. The build
   being changed must dump exactly what the 2.16-2 build dumped: the port of
   the editor onto the new storage is behaviour-preserving for ASCII by
   construction of the test, not by inspection.
3. **On the target**, as before: `packages/tests/sisong.sh` from
   `boot-test.sh`, extended with Unicode typing, width and escape checks.

## Stages

- [ ] 0. Selftest driver in the 2.16 code; baseline dumps recorded.
- [ ] 1. `src/text`: codec, width table, `TextStore`, `LineStore`, host tests.
- [ ] 2. `RopeStore` and the three-way fuzz.
- [ ] 3. The editor on `TextStore` through `DocLine`, storage `LineStore`;
        selftest dumps equal the baseline.
- [ ] 4. Unicode: key input, cell widths in painter, cursor and mouse,
        escapes, line endings and BOM, clipboard, find.
- [ ] 5. Storage switched to `RopeStore`.
- [ ] 6. Undo as snapshots.
- [ ] 7. The auto-saver writes a snapshot from its own thread.
- [ ] 8. Sisong 3.0: version, About, package, image, boot test.
