/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef GRAPHICS_FONTS_HIGHRES_FONT_OVERLAY_H
#define GRAPHICS_FONTS_HIGHRES_FONT_OVERLAY_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/ustr.h"
#include "common/array.h"
#include "common/rect.h"
#include "common/ptr.h"
#include "common/mutex.h"
#include "common/noncopyable.h"
#include "common/stream.h"
#include "common/types.h"

namespace Graphics {

class Font;
class Surface;
#ifdef USE_FREETYPE2
class GlyphAtlas;
#endif

/**
 * @defgroup graphics_fonts_highres_overlay High-resolution font overlay
 * @ingroup graphics
 *
 * @brief HighResFontOverlay class implementing the deferred vector text
 * pipeline from the ScummVM font modernization RFC.
 *
 * @{
 */

/**
 * Intercepts legacy low-resolution text draw requests, queues them, and
 * renders them with a vector (TrueType) font directly onto the final,
 * already-upscaled hardware surface -- so upscaling filters never touch
 * the text, and it stays crisp at native display resolution.
 *
 * Pipeline (see the RFC for the full diagram):
 *   Game logic calls enqueueText() while processing the current frame's
 *   script/dialogue output (this is the only part engine code needs to
 *   call). Once the backend has scaled the low-resolution game surface
 *   up to native resolution, it calls renderOverlay() once, which walks
 *   the queue, rasterizes/caches glyphs via GlyphAtlas, and blits them.
 *
 * This class is always declared, regardless of whether ScummVM was
 * built with FreeType2 support: loadTrueTypeFont() simply always fails
 * (returning false) and isActive() always returns false when
 * USE_FREETYPE2 is not defined, so engine call sites only need a single
 * isActive() check rather than '#ifdef USE_FREETYPE2' scattered through
 * engine code. Any failure -- missing FreeType2 support, a missing or
 * corrupt font file, or an atlas that cannot fit a glyph -- is designed
 * to fall back to the original raster drawing path, never to abort or
 * crash the game.
 */
class HighResFontOverlay : public Common::NonCopyable {
public:
	HighResFontOverlay();
	~HighResFontOverlay();

	/**
	 * Loads the vector font used to render queued text.
	 *
	 * Per the "no disk I/O in the main loop" rule, this is meant to be
	 * called once during engine initialization, not per-frame.
	 *
	 * @param fontPath      Path to a TTF/OTF file, resolved the same
	 *                      way Graphics::loadTTFFontFromArchive()
	 *                      resolves paths (theme/font archives, the
	 *                      extra path, etc).
	 * @param basePointSize Point size to rasterize at.
	 * @return true on success. false if this build lacks FreeType2
	 *         support, or the font could not be found/parsed -- either
	 *         way, isActive() will report false and callers should use
	 *         the legacy raster path.
	 */
	bool loadTrueTypeFont(const Common::String &fontPath, int basePointSize);

	/**
	 * Unicode-native counterpart of loadTrueTypeFont(const Common::String&, ...)
	 * for engines that resolve font data through their own resource or
	 * archive system rather than a filesystem path or ScummVM's bundled
	 * theme/font archive (BladeRunner's MIX-file resources, for
	 * instance). Takes an already-open stream directly: this class does
	 * not need to know how any particular engine's resource lookup
	 * works, only how to hand a stream to Graphics::loadTTFFont().
	 *
	 * @param stream        An already-open stream positioned at the
	 *                      start of TTF/OTF font data.
	 * @param dispose       Whether this call takes ownership of 'stream'
	 *                      (matching Graphics::loadTTFFont()'s own
	 *                      contract).
	 * @param basePointSize Point size to rasterize at.
	 */
	bool loadTrueTypeFont(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, int basePointSize);

	/** True if a font was loaded successfully and this overlay can be used. */
	bool isActive() const;

	/**
	 * Sets the scale factor between the legacy logical game resolution
	 * and the native/hardware resolution the overlay renders into.
	 * Queried dynamically from OSystem/the backend by callers -- this
	 * class does not hardcode any target resolution.
	 */
	void setScaleFactors(float scaleX, float scaleY);

	/**
	 * Queues a run of plain text for deferred high-resolution
	 * rendering. Must only be called for text that does not require
	 * mid-string state changes (color/newline escape codes, multi-byte
	 * CJK, etc.) that this pipeline does not model; callers are
	 * responsible for falling back to the legacy path for those cases.
	 *
	 * @param text               The text to draw, in the font's native encoding.
	 * @param legacyCoords       Top-left draw position, in legacy/logical coordinates.
	 * @param originalRasterWidth Total width, in legacy pixels, the original
	 *                            raster font would have occupied. Used to
	 *                            compute the kerning adjustment that keeps
	 *                            the vector string's width matching the
	 *                            layout the game script/UI expects.
	 * @param colorIndex         Palette color index the legacy engine
	 *                            requested for this text.
	 * @return true if the request was queued. false if the overlay is
	 *         not active, or the queue watchdog discarded a backlog --
	 *         callers must fall back to the raster path when false is
	 *         returned.
	 */
	bool enqueueText(const Common::String &text, const Common::Point &legacyCoords,
		int originalRasterWidth, uint32 colorIndex);

	/**
	 * Unicode-native counterpart of enqueueText(const Common::String&, ...),
	 * for callers whose source text is not representable as a single
	 * byte per character (accented Latin, Cyrillic, etc. -- anything
	 * outside plain CJK/RTL layout, which remains out of scope; see
	 * graphics/fonts/HIGHRES_FONTS.md). Iterates actual Unicode
	 * codepoints rather than bytes, so it is the correct overload for
	 * any caller working in Common::U32String already (as
	 * BladeRunner's TTF subtitle path does) rather than narrowing to
	 * Common::String first, which would corrupt anything outside
	 * Latin-1.
	 */
	bool enqueueText(const Common::U32String &text, const Common::Point &legacyCoords,
		int originalRasterWidth, uint32 colorIndex);

	/**
	 * Renders (and clears) every queued text run onto
	 * 'nativeScreenSurface'. Expected to be called by the backend once
	 * per frame, directly after the legacy game surface has been
	 * scaled/blitted to the native surface and before it is presented
	 * to the display.
	 */
	void renderOverlay(Graphics::Surface *nativeScreenSurface);

	/** Discards any queued (not yet rendered) text without drawing it. */
	void clearQueue();

	/**
	 * Discards cached glyph atlas contents. Engines should call this on
	 * room/scene transitions to bound memory use (see the RFC's
	 * "room transition flush" eviction strategy).
	 */
	void flushGlyphCache();

private:
	struct QueuedText {
		Common::U32String text;
		Common::Point coords;
		int targetWidth;
		uint32 color;
	};

	bool enqueueTextInternal(const Common::U32String &text, const Common::Point &legacyCoords,
		int originalRasterWidth, uint32 colorIndex);

	Common::Point scaleCoordinatesToNative(const Common::Point &legacyCoords) const;
	void renderQueuedText(const QueuedText &entry, Graphics::Surface *dst);

	// Owned font reference. Using ScopedPtr (rather than a raw pointer
	// with a hand-written destructor) gives single, unambiguous
	// ownership with no possibility of a leaked or double-freed Font.
	Common::ScopedPtr<Font> _font;

#ifdef USE_FREETYPE2
	Common::ScopedPtr<GlyphAtlas> _atlas;
#endif

	Common::Array<QueuedText> _renderQueue;

	// Guards _renderQueue. The current integration (engine hook and
	// backend composition both run on the main thread once per frame)
	// does not exercise this concurrently, but the queue/dequeue split
	// mirrors the RFC's logical-thread/render-thread pipeline and nothing
	// prevents a future caller (e.g. an engine driving subtitles from an
	// audio callback) from enqueueing off the main thread, so the queue
	// is protected defensively rather than left a latent hazard.
	mutable Common::Mutex _queueMutex;

	bool _isInitialized;
	int _fontPointSize;
	float _scaleX, _scaleY;

	// Watchdog: if enqueueText() is called this many times without an
	// intervening renderOverlay() flush, something downstream is stuck
	// (e.g. the backend stopped presenting frames); rather than growing
	// without bound, the queue is dropped and a warning logged, and the
	// caller is told to fall back for the current frame.
	static const uint kMaxQueueDepth = 512;
};

/** @} */

} // End of namespace Graphics

#endif
