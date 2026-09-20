# ProseApps

Applications for [Prose](https://github.com/albanread) — the unofficial,
experimental port of Haiku to Apple Silicon. Native apps, built on the Be
API as Haiku carries it: yellow window tabs, message-based scripting, and
respect for the machine.

| App | What it is |
|---|---|
| [ProseWriter](ProseWriter/) | A word processor: styled text, pages, tables, inline images, headers/footers, named styles, spell check with red squiggles, find & replace, RTF interchange, `hey` scripting — self-tested at 114 cases and driven live in QEMU |

## Ports

Software written by others for BeOS and Haiku, brought over to Prose: the
upstream source as it came, then the changes it took to run well on arm64
and a current system, each as its own commit. See [ports/](ports/).

| Port | What it is | Upstream | Licence |
|---|---|---|---|
| [Sisong](ports/Sisong/) | A programmer's editor and small IDE: tabs, syntax colouring, a function list, brace matching, find in files, projects with build scripts and a compile pane that jumps to errors | [HaikuArchives/Sisong](https://github.com/HaikuArchives/Sisong), ©2009 Caitlin Shaw | GPL 3 |

A port keeps its author's licence, which need not be this repository's:
each port's directory carries its own.

## Working on them

Each app carries its own README, build files (cross-compiled for Haiku
arm64), harness scripts, and documentation. The shared development
workflow — build on the Mac, deploy and verify in a QEMU/HVF guest — is
described in each app's `vm/` scripts and the repo documentation it was
developed in.

ProseWriter is MIT licensed. It stands on the shoulders of Haiku and the
Be engineers before them; the credit is theirs.
