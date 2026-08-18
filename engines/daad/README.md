# DAAD Engine — Status Notes

Mirrors `mark-temporary/scummvm` master (commit `2d85aeb2`), the head of
upstream PR [scummvm/scummvm#7840](https://github.com/scummvm/scummvm/pull/7840)
("ENGINES: Add DAAD engine"), open/unmerged as of 2026-08-18.

## What this is

A ScummVM front-end for **ADP**, [jlcebrian's](https://github.com/jlcebrian/ADP)
portable interpreter for **DAAD** (*Diseñador de Aventuras AD*) game databases —
covering Spectrum, CPC, C64, MSX, PCW, Atari ST, Amiga and PC targets.

## Layout

- `adp/` — the vendored ADP interpreter, **unchanged from upstream** (per
  `module.mk`) — ~1MB across `include/` (29 headers) and `src-common/`
  (41 source files: `ddb_*` interpreter core, `dim_*`/`dmg_*` disk & bitmap
  formats).
- `daad.cpp` / `.h` — engine entry point (102 / 71 lines).
- `daad_file.cpp` (691 lines) — replaces ADP's file layer: game data reads
  go through `Common::FSNode`, save/transcript writes go through
  `g_system->getSavefileManager()`.
- `daad_vid.cpp` (2089 lines) — implements ADP's `VID_*` ABI on top of
  `OSystem` (palette, `copyRectToScreen`, event pump, clipboard, audio via
  `Audio::Mixer`).
- `adp_prefix.h` — renames ADP's ~200 unprefixed C globals (`Free`, `Abort`,
  `charset`, …) to `adp_*` to avoid link collisions with other engines;
  force-included via `-include` in `module.mk`, so the vendored sources need
  no patching.
- `detection.cpp` — 10 games: 8 classic-era titles (Chichen Itza, Cozumel,
  Jabato, La Tumba del Faraón, La Aventura Original, La Aventura Espacial,
  Los Vengadores: Carelli, El Templo Maya) plus the two modern releases named
  in the PR (Rabenstein, City of Gold).

## Save support

Native ScummVM save-slot UI is intentionally not exposed (`hasFeature()`
only returns `kSupportsReturnToLauncher`). DAAD's own SAVE/LOAD condacts
call ADP's `File_Create`/`File_Open`, which `daad_file.cpp` routes to
`SaveFileManager::openForSaving`/`openForLoading` — so saves persist through
ScummVM's normal save-file backend, just triggered in-game rather than from
the launcher menu. Stated explicitly in code comments, not left implicit.

## Verified against an independent LLM-generated review (2026-08-18)

That review's PR-level research checked out: real PRs, correct titles,
correct merge states, correct stats for #7840 (42,479 additions / 80 files /
2 commits — all confirmed against the GitHub API). Its code-level concerns
did **not** hold up under direct inspection of this branch:

| Concern raised | Verified finding |
|---|---|
| OSystem abstraction unproven | `daad_vid.cpp` calls `g_system->` throughout — palette, blit, timing, events, clipboard (40 call sites) |
| std::/malloc contamination | Zero `std::` in the shim; one matched `malloc`/`free` pair, correct as written (owns a buffer handed to `Audio::makeRawStream(..., DisposeAfterUse::YES)`, which expects and frees a raw allocation itself) |
| Endian/struct-cast safety | No `reinterpret_cast` anywhere in the shim; the only `memcpy` calls are pixel-buffer/palette copies, not raw file-struct parsing |
| Save-state policy unclear | Explicitly resolved — see above, documented in-code |
| `adp_prefix.h` possibly generated/unmaintained | Hand-written and documented; only its symbol *list* is mechanically derived from the vendored headers (avoids missed renames) — the shim logic and rationale are not generated |
| No vendoring provenance statement | `module.mk` states "Unchanged from its parent repository" explicitly |

No claim is made here about the ~1MB of vendored ADP code itself, which this
PR deliberately does not modify.
