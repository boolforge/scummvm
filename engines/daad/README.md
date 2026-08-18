# DAAD Engine — Provenance Notes

This branch (`daad`) mirrors the `master` branch of
[mark-temporary/scummvm](https://github.com/mark-temporary/scummvm) at commit
`2d85aeb251783781dc59bb48fe7a205299fb5294`, the head of upstream pull request
[scummvm/scummvm#7840](https://github.com/scummvm/scummvm/pull/7840)
("ENGINES: Add DAAD engine"), open and unmerged as of 2026-08-18.

**Engine source:** `engines/daad/`

Confirmed standalone top-level ScummVM engine (not a Glk sub-engine — no
`daad` entry exists under `engines/glk/`, and this engine includes its own
video/graphics handling in `daad_vid.cpp`, which wouldn't fit Glk's
text-window model).

Files present at branch creation time:
- `daad.cpp` / `daad.h` — core engine
- `daad_file.cpp` — file/data handling
- `daad_vid.cpp` — video/graphics
- `detection.cpp` / `detection.h` / `metaengine.cpp` — ScummVM plugin boilerplate
- `adp/` — subdirectory (not yet inventoried)
- `configure.engine`, `module.mk` — build integration

No README existed upstream in this directory — this file was added locally
to track provenance and merge status.
