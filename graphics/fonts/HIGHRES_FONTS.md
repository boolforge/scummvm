# High-resolution vector font overlay

Status: **experimental, opt-in, disabled by default**. Currently integrated
into the SCUMM engine (dialogue text) and the SurfaceSDL backend only.

## What this is

ScummVM's classic engines draw text as low-resolution bitmap glyphs directly
into the same logical game surface as everything else, so upscaling filters
that make pixel art look good on modern displays make text blurry or jagged
right along with it.

This subsystem lets an engine draw dialogue/UI text with a scalable vector
(TrueType/OpenType) font instead, composited *after* the game surface has
already been scaled up to native display resolution -- so the text is crisp
at whatever resolution the player's screen actually is, independent of the
game's original internal resolution.

## Pipeline

```
Engine code (e.g. ScummEngine::drawString)
    | enqueueText(text, legacyCoords, originalRasterWidth, colorRGB)
    v
Graphics::HighResFontOverlay          (graphics/fonts/highres_font_overlay.h)
    - one owned Font + GlyphAtlas per overlay instance
    - queues requests, does no rendering yet
    |
    | (queue is drained once per frame)
    v
Backend composition (e.g. SurfaceSdlGraphicsManager::drawHighResFontOverlay)
    - locks the final, already-upscaled hardware surface
    - calls Graphics::HighResFontManager::renderActiveOverlays()
    v
Graphics::GlyphAtlas                  (graphics/fonts/glyph_atlas.h)
    - rasterizes+caches each glyph as an 8-bit alpha mask (shelf-packed)
    - reused across frames until flush() (room/scene transition)
```

`Graphics::HighResFontManager` (graphics/fonts/highres_font_manager.h) is a
keyed registry sitting on top of this: each engine/subsystem that wants a
high-resolution font requests one under its own id (SCUMM currently uses
`"scumm_dialogue"`), and backends composite every currently-active overlay
with a single `renderActiveOverlays()` call, without needing to know which
engines exist.

## Why it is safe to enable blindly

Every failure mode -- this build lacking `USE_FREETYPE2`, a missing/corrupt
font file, an atlas that cannot fit a glyph, a render queue that backs up --
is designed to fall back silently to the original raster drawing path, never
to crash or change behavior for anyone who has not opted in:

* `HighResFontOverlay` is always declared, `USE_FREETYPE2` or not; when the
  build lacks FreeType2, `loadTrueTypeFont()` simply always returns `false`
  and `isActive()` always returns `false`. Call sites only ever need one
  `isActive()` check, never a `#ifdef USE_FREETYPE2`.
* `enqueueText()` returns `false` (not queued) if the overlay is inactive,
  if the request is a string this pipeline cannot safely represent (see
  "Known limitations" below), or if a stalled backend let the queue back up
  past `kMaxQueueDepth` -- callers are expected to fall back to their legacy
  path whenever it returns `false`.
* Nothing here is ever written to save games. `_highResOverlay` and related
  state are purely ephemeral, in-memory UI/rendering state, reconstructed
  from `ConfMan` at each session, never serialized.

## Config keys (currently SCUMM-specific; see "Migrating another engine")

| Key                          | Type   | Default          |
|-------------------------------|--------|------------------|
| `highres_fonts`                | bool   | `false`          |
| `highres_font_path`            | string | `FreeSans.ttf`   |
| `highres_font_point_size`      | int    | `18`             |

Also exposed as a Launcher checkbox ("Use high-resolution vector fonts") on
every SCUMM game's Engine options tab. Toggling it while a game is running
takes effect within one `scummLoop()` iteration (a forced full redraw), no
restart required.

Debug logging: `--debugflags=highres_fonts` (SCUMM's own `DEBUG_HIGHRES_FONTS`
channel; see `engines/scumm/detection.h`/`detection.cpp`). Core
`graphics/fonts/*` code logs via the generic, channel-less `debug()` instead
of a hardcoded engine channel, since that code has no business depending on
any one engine's debug enum.

## Known limitations (by design, for this PoC pass)

* **SCUMM only intercepts "plain" text runs.** Strings containing embedded
  SCUMM formatting opcodes (color changes, forced newlines, HE overlay
  markers) or, in CJK mode, multi-byte high-bit sequences, carry mid-string
  state changes this pipeline does not model, and always use the legacy
  per-character raster path -- see the comment block in
  `ScummEngine::drawString()` (`engines/scumm/string.cpp`).
* **One backend.** Only `SurfaceSdlGraphicsManager` composites the overlay.
  Other backends (and any OpenGL-specific rendering path within the SDL
  backend) do not yet call `drawHighResFontOverlay()`.
* **No CJK/RTL vector layout.** `HighResFontOverlay::enqueueText()` takes an
  8-bit `Common::String`; wide/right-to-left scripts fall back to the legacy
  path today (see the plain-run check above) rather than being laid out
  incorrectly.
* **The render queue mutex is currently defensive, not load-bearing.** Both
  the engine hook and the backend composition call run on the main thread
  once per frame in the current integration, so nothing exercises the
  `Common::Mutex` concurrently today. It is there so an engine driving text
  from another thread in the future (e.g. an audio-callback-timed subtitle
  system) does not have to retrofit locking later.

## Backend coverage

| Backend | Status |
|---|---|
| `surfacesdl` (default `sdl`) | Composited directly; built and linked. |
| `opengl` (base class) | Composited via a blended texture; built and linked. |
| `openglsdl` | Inherits the OpenGL hook (`OpenGLSdlGraphicsManager::updateScreen()` calls `OpenGL::OpenGLGraphicsManager::updateScreen()`); built and linked. |
| `opendingux` | Inherits the SurfaceSDL hook (no `updateScreen()` override); configured with `--backend=opendingux` and built successfully on this (x86 Linux) host. |
| `openpandora` | Inherits the SurfaceSDL hook the same way, but currently fails to build in this checkout for an unrelated, pre-existing reason: `backends/platform/openpandora/op-backend.o` passes a `Common::String` to an `FSDirectory` constructor that no longer accepts one (an API mismatch elsewhere in the tree, not touched by this work). Not something this pass attempted to fix. |
| `maemosdl`, `riscossdl` | Subclass `SurfaceSdlGraphicsManager`/`OpenGLGraphicsManager` without overriding `updateScreen()`, so they inherit a hook the same way opendingux does, but neither is reachable through this repo's `--backend=` flag (only `maemo`, not `maemosdl`, is listed; `riscossdl` isn't listed at all), and neither was built end-to-end. |
| `miyoo` | Overrides a differently-signatured internal `updateScreen(SDL_Rect *, int)`, not this class's public `updateScreen()`, so it is very likely still covered by the SurfaceSDL hook -- not individually confirmed. |
| `android`, `atari`, `dc`, `ios`/`ios7`, `n64`, `psp`, `psp2`, `3ds`, `switch`, `wii`, `samsungtv` | **Not attempted.** These require cross-compilation toolchains and platform SDKs (devkitARM/devkitPro, PSP/PSP2 SDKs, a Dreamcast KOS toolchain, etc.) that are not present in this environment, and several render through entirely different, platform-specific 2D/3D APIs rather than SDL or this codebase's own OpenGL wrapper. Writing composition code for them here would not be compiled or checked in any way -- pure guesswork rather than the same empirically-verified engineering as the rest of this document. A contributor with access to the relevant SDK should follow the same pattern as the OpenGL hook (render to an intermediate alpha surface sized to the game's on-screen rectangle, then composite it as a blended textured quad/sprite through that platform's native draw call) as a starting point. |
| `null` | Not applicable -- headless, no screen to composite onto. |

## Engines already using Graphics::Font/TTF directly

A number of engines (agds, buried, glk, gnap, grim, mohawk, mtropolis,
myst3, neverhood, petka, phoenixvr, pink, vcruise, in addition to
bladerunner) already call loadTTFFont()/loadTTFFontFromArchive()
directly, which makes them structurally closer to a "just hook the
existing draw call" migration than SCUMM's was. That does not mean
every one of them needs or benefits from migrating, though:

- **myst3 does not need this and should not be migrated as-is.**
  `FontSubtitles::drawToTexture()` (engines/myst3/subtitles.cpp)
  already recomputes the display/original-resolution scale ratio every
  time it draws, and when that ratio changes it deletes and reloads
  the font at the new scale and recreates its texture at the new size
  -- i.e. it already renders directly at whatever the current display
  scale is, with no fixed low-resolution intermediate surface for an
  upscale filter to blur. There is no upscale-blur problem here for
  this pipeline to solve, and forcing this engine onto it would add
  complexity (a second loaded font instance, kerning math meant to
  match a legacy raster width that does not exist here) with no
  visible benefit, and a real risk of interacting badly with myst3's
  own existing rescaling logic. Checked by reading the actual
  drawToTexture() implementation, not assumed from the class using
  Graphics::Font.
- **mtropolis is a plausible candidate, not yet confirmed either way.**
  Its Subtitles class (engines/mtropolis/subtitles.cpp) loads a TTF
  font once and draws directly onto a Graphics::ManagedSurface that
  gets blitted onto a `Window`'s own surface -- but whether that
  Window's surface is a fixed logical resolution (this pipeline's
  target case) or something already scale-aware the way myst3's is
  was not resolved before this pass ended. Needs the same "read the
  actual resolution/scaling model before writing any code" treatment
  the SCUMM, BladeRunner, and myst3 investigations all got, not an
  assumption in either direction.
- SCI's text rendering (engines/sci/graphics/text16.cpp, ~1000 lines)
  was surveyed and found substantially more complex than either prior
  migration: it uses SCI's own internal GfxFont bitmap-font class
  hierarchy rather than Graphics::Font directly (a Graphics::Font-based
  path only exists for its separate Mac-font rendering mode), has an
  in-band `|code|` control-code format for color/font changes plus a
  distinct `|r|` "reference rectangle" code used for interactive
  hotspots in at least one game, and has version-specific
  (SCI0/SCI01/SCI1) Shift-JIS punctuation handling for line-wrapping
  that would need its own careful exclusion, the same way this project
  excludes CJK elsewhere. None of this is a reason not to migrate SCI
  eventually, but it is real enough additional scope (likely comparable
  to the SCUMM migration's, not BladeRunner's) that it deserves its own
  dedicated pass rather than being folded in alongside a faster-moving
  survey of already-TTF-based engines.

## Migrating another engine

BladeRunner's subtitle renderer (`engines/bladerunner/subtitles.cpp`) has
been migrated (see its own commit history for the specifics); it was
picked first because, like the engines surveyed above, it already loaded
its own isolated `loadTTFFontFromArchive()`/stream-based font independent
of the shared `Graphics::FontManager`. Checklist for migrating any other
engine's own ad hoc TTF (or, per SCUMM's example, non-TTF) text rendering
onto this shared pipeline:

1. Pick a stable, engine-specific id string (e.g. `"bladerunner_subtitles"`).
2. Replace the engine's direct `Graphics::loadTTFFontFromArchive()`/manual
   `Font*` ownership with a single `Graphics::HighResFontManager::instance()
   .loadFont(FontConfig{path, pointSize, id})` call during engine init.
3. Replace whatever the engine's existing draw call does with
   `overlay->enqueueText(...)`, gated by `overlay->isActive()`; keep the
   engine's legacy raster path completely intact as the `else` branch.
4. Identify the engine's own per-string bounding-box/pen-position
   bookkeeping (SCUMM's is `CharsetRenderer::_left`/`_str`) and replicate it
   after a successful `enqueueText()`, the same way
   `ScummEngine::drawString()` does, so any code downstream that reads that
   state keeps working even when no raster pixels were touched.
5. Call `overlay->flushGlyphCache()` (or
   `HighResFontManager::instance().flushAllGlyphCaches()`) at the engine's
   own room/scene transition point.
6. Add a backend composition call if the engine is only ever used with a
   backend that does not already call `drawHighResFontOverlay()` (SCUMM's
   SDL integration covers any engine using the SurfaceSDL backend already,
   since composition happens in `HighResFontManager::renderActiveOverlays()`,
   which iterates every registered id, not just SCUMM's).
7. Add an `ExtraGuiOption`/config default for the new engine, following
   `engines/scumm/metaengine.cpp`'s `highResFontsOption` as a template.

Do not attempt more than one engine's migration in a single change --
this pass deliberately touched only SCUMM to keep the surface area (and
review burden) of each step contained.
