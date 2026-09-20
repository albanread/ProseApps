# ProseApps

Applications for [Prose](https://github.com/albanread) — the unofficial,
experimental port of Haiku to Apple Silicon. Native apps, built on the Be
API as Haiku carries it: yellow window tabs, message-based scripting, and
respect for the machine.

| App | What it is |
|---|---|
| [ProseWriter](ProseWriter/) | A word processor: styled text, pages, tables, inline images, headers/footers, named styles, spell check with red squiggles, find & replace, RTF interchange, `hey` scripting — self-tested at 114 cases and driven live in QEMU |

Each app carries its own README, build files (cross-compiled for Haiku
arm64), harness scripts, and documentation. The shared development
workflow — build on the Mac, deploy and verify in a QEMU/HVF guest — is
described in each app's `vm/` scripts and the repo documentation it was
developed in.

ProseWriter is MIT licensed. It stands on the shoulders of Haiku and the
Be engineers before them; the credit is theirs.
