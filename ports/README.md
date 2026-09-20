# Ports

Applications written by other people for BeOS and Haiku, ported to
[Prose](https://github.com/albanread/Prose) (Haiku on Apple silicon: arm64,
gcc 13, a current Be API). The apps at the top level of this repository are
written for Prose; these are adopted.

| Port | Version | Upstream | Licence | Package |
|---|---|---|---|---|
| [Sisong](Sisong/) | 2.16 | [HaikuArchives/Sisong](https://github.com/HaikuArchives/Sisong) `a79c29e` | GPL 3 | `sisong`, in the Prose image |

## How a port is kept

One directory per port, laid out as upstream lays it out, so that upstream's
later changes can still be compared and merged.

1. **The first commit is upstream's tree, unchanged**, and says which
   commit of which repository it is. Everything after it is the port, so
   `git log -- ports/<Name>` is the port's history and
   `git diff <import commit> -- ports/<Name>` is exactly what it changed.
   Build products that upstream carries (an x86 executable, say) are left
   out, and the import commit names them.
2. **`PORT.md`** in the port's directory records where the source came
   from, the licence, what was changed and why, how to build and test it,
   and what is known not to work.
3. **The licence is the author's.** Upstream's licence file stays where it
   was, and the port's changes are offered under the same terms. It need not
   match the licence of anything else in this repository: each directory
   here is its own work, side by side with the others.
4. **What the port adds lives apart from upstream's files** where it can: a
   `Makefile` at the port's top, and a `prose/` directory for resources, the
   icon and tests. Changes to upstream's own files are kept to what the
   port needs, with a comment where the reason is not obvious.
5. **A port is done when it is a package.** The recipe is in the Prose
   repository (`packages/builder/overlay/`), built by `prosepkg` from a
   commit of this repository, and the package is listed in the Prose image's
   profile with a boot test (`packages/tests/`) that starts the program on
   the target and checks it works.

## What "ported" means here

It compiles almost at once; that is not the port. Code of this age was
written for 32-bit x86 and gcc 2.95, and what goes wrong on Prose goes wrong
at run time:

- **`char` is unsigned on arm64.** Comparisons with -1, negative deltas in a
  `char`, `fgetc()` kept in a `char`.
- **Pointers and `long` are 64 bits.** `%x` for a pointer, `%ld` for an
  `int32` (in `sscanf` that writes past the variable), pointers carried in
  `int32` message fields.
- **gcc 13 at `-O2` takes undefined behaviour at its word.** A function
  declared to return a value that falls off its end does not return: the
  code runs on into whatever follows it.
- **The system is not the one it was written on.** Paths that were the
  author's own, files written beside the executable (an installed package
  is read-only), fonts and metrics, programs it expects to find.
- **What it does behind the user's back.** A program in the Prose image
  does not phone home.

Each port's `PORT.md` lists what was found under these heads.
