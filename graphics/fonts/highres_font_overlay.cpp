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

#include "graphics/fonts/highres_font_overlay.h"

#include "graphics/font.h"
#include "graphics/surface.h"
#include "graphics/pixelformat.h"
#include "common/debug.h"
#include "common/file.h"

#include <math.h>

#ifdef USE_FREETYPE2
#include "graphics/fonts/glyph_atlas.h"
#include "graphics/fonts/ttf.h"
#endif

namespace Graphics {

namespace {

/**
 * Alpha-blends a single glyph coverage sample at (x, y).
 *
 * Two destination kinds are supported, because this same blit code
 * backs both the SurfaceSDL backend (which blends directly onto the
 * already-composited, fully opaque final frame -- no alpha channel
 * involved) and the OpenGL backend (which renders onto an
 * intermediate, separately-alpha-blended texture surface, so the
 * *coverage itself* must survive as this surface's real alpha channel
 * for the GPU's own blending to composite it correctly later):
 *
 * - Destination format has an alpha channel: write coverage straight
 *   through as that pixel's alpha (with the requested ink color as
 *   RGB), rather than blending against whatever this intermediate
 *   surface's existing contents are. Blending here and *again* on the
 *   GPU would double-attenuate partially-covered glyph edges.
 * - Destination format has no alpha channel (or is CLUT8): blend
 *   directly against the existing destination pixel, exactly as
 *   before. CLUT8 can still only threshold (matching the same
 *   documented limitation graphics/fonts/ttf.cpp itself accepts for
 *   indexed-color surfaces), since there is no anti-aliasing ramp to
 *   blend against in an arbitrary, unknown palette.
 */
void blendCoverageSample(Graphics::Surface *dst, int x, int y, uint8 coverage, uint8 r, uint8 g, uint8 b) {
	if (coverage == 0)
		return;
	if (x < 0 || y < 0 || x >= dst->w || y >= dst->h)
		return;

	const Graphics::PixelFormat &format = dst->format;

	if (format.isCLUT8()) {
		if (coverage >= 0x80)
			*(uint8 *)dst->getBasePtr(x, y) = (uint8)format.RGBToColor(r, g, b);
		return;
	}

	if (format.aBits() > 0) {
		// Straight (non-premultiplied) alpha: let whatever composites
		// this surface next (a GPU blend against the game texture) do
		// the actual blending, exactly once.
		uint32 outColor = format.ARGBToColor(coverage, r, g, b);
		switch (format.bytesPerPixel) {
		case 2:
			*(uint16 *)dst->getBasePtr(x, y) = (uint16)outColor;
			break;
		case 4:
			*(uint32 *)dst->getBasePtr(x, y) = outColor;
			break;
		default:
			break;
		}
		return;
	}

	uint32 dstColor = dst->getPixel(x, y);
	uint8 dr, dg, db;
	format.colorToRGB(dstColor, dr, dg, db);

	const uint16 a = coverage;
	const uint16 ia = 255 - coverage;
	uint8 outR = (uint8)((r * a + dr * ia) / 255);
	uint8 outG = (uint8)((g * a + dg * ia) / 255);
	uint8 outB = (uint8)((b * a + db * ia) / 255);

	uint32 outColor = format.RGBToColor(outR, outG, outB);

	switch (format.bytesPerPixel) {
	case 1:
		*(uint8 *)dst->getBasePtr(x, y) = (uint8)outColor;
		break;
	case 2:
		*(uint16 *)dst->getBasePtr(x, y) = (uint16)outColor;
		break;
	case 4:
		*(uint32 *)dst->getBasePtr(x, y) = outColor;
		break;
	default:
		break;
	}
}

/**
 * A stray or corrupt config value (0, negative, or absurdly large)
 * should degrade to a sane size rather than misbehave -- FreeType's
 * own behavior for a non-positive size is unspecified, and nothing in
 * this pipeline needs to support a font too small to read or large
 * enough to blow the atlas's fixed budget on one glyph.
 */
int clampPointSize(int basePointSize) {
	if (basePointSize < 4 || basePointSize > 128) {
		int clamped = CLIP(basePointSize, 4, 128);
		warning("HighResFontOverlay: requested point size %d out of sane range, using %d instead",
			basePointSize, clamped);
		return clamped;
	}
	return basePointSize;
}

} // End of anonymous namespace

HighResFontOverlay::HighResFontOverlay() :
	_isInitialized(false), _fontPointSize(0), _scaleX(1.0f), _scaleY(1.0f) {
}

HighResFontOverlay::~HighResFontOverlay() {
	// _font (and _atlas, when built with FreeType2) are ScopedPtrs and
	// clean themselves up here with no explicit delete required.
}

bool HighResFontOverlay::loadTrueTypeFont(const Common::String &fontPath, int basePointSize) {
	_isInitialized = false;
	_font.reset();
#ifdef USE_FREETYPE2
	_atlas.reset();
#endif

	basePointSize = clampPointSize(basePointSize);

#ifndef USE_FREETYPE2
	debug(1, "HighResFontOverlay: built without USE_FREETYPE2; high-resolution fonts are unavailable, falling back to raster rendering");
	return false;
#else
	Font *font = nullptr;

	// Fonts are loaded once here (engine init time), never per-frame:
	// no disk I/O happens on the main game loop's hot path.
	Common::File *file = new Common::File();
	if (file->open(Common::Path(fontPath))) {
		font = Graphics::loadTTFFont(file, DisposeAfterUse::YES, basePointSize);
	} else {
		delete file;
		// Fall back to resolving the path inside ScummVM's bundled
		// theme/font archive (e.g. a font shipped in fonts.dat).
		font = Graphics::loadTTFFontFromArchive(fontPath, basePointSize);
	}

	if (!font) {
		debug(1, "HighResFontOverlay: could not load TTF font '%s' at size %d; falling back to raster rendering",
			fontPath.c_str(), basePointSize);
		return false;
	}

	_font.reset(font);
	_atlas.reset(new GlyphAtlas());
	_fontPointSize = basePointSize;
	_isInitialized = true;

	debug(1, "HighResFontOverlay: loaded '%s' at %d pt", fontPath.c_str(), basePointSize);
	return true;
#endif
}

bool HighResFontOverlay::loadTrueTypeFont(Common::SeekableReadStream *stream, DisposeAfterUse::Flag dispose, int basePointSize) {
	_isInitialized = false;
	_font.reset();
#ifdef USE_FREETYPE2
	_atlas.reset();
#endif

	if (!stream) {
		debug(1, "HighResFontOverlay: loadTrueTypeFont() called with a null stream; falling back to raster rendering");
		return false;
	}

	basePointSize = clampPointSize(basePointSize);

#ifndef USE_FREETYPE2
	if (dispose == DisposeAfterUse::YES)
		delete stream;
	debug(1, "HighResFontOverlay: built without USE_FREETYPE2; high-resolution fonts are unavailable, falling back to raster rendering");
	return false;
#else
	Font *font = Graphics::loadTTFFont(stream, dispose, basePointSize);
	if (!font) {
		debug(1, "HighResFontOverlay: could not parse TTF data from stream at size %d; falling back to raster rendering", basePointSize);
		return false;
	}

	_font.reset(font);
	_atlas.reset(new GlyphAtlas());
	_fontPointSize = basePointSize;
	_isInitialized = true;

	debug(1, "HighResFontOverlay: loaded font from stream at %d pt", basePointSize);
	return true;
#endif
}

bool HighResFontOverlay::isActive() const {
	return _isInitialized && _font;
}

void HighResFontOverlay::setScaleFactors(float scaleX, float scaleY) {
	_scaleX = scaleX;
	_scaleY = scaleY;
}

Common::Point HighResFontOverlay::scaleCoordinatesToNative(const Common::Point &legacyCoords) const {
	return Common::Point(
		(int16)floor(legacyCoords.x * _scaleX + 0.5f),
		(int16)floor(legacyCoords.y * _scaleY + 0.5f));
}

bool HighResFontOverlay::enqueueText(const Common::String &text, const Common::Point &legacyCoords,
		int originalRasterWidth, uint32 colorIndex) {
	// SCUMM's strings are single-byte (extended ASCII); widening each
	// byte to its own codepoint is lossless for that caller and lets
	// the actual queue/render path be Unicode-native throughout,
	// rather than needing a second, byte-oriented code path.
	Common::U32String widened;
	for (uint i = 0; i < text.size(); i++)
		widened += (uint32)(byte)text[i];
	return enqueueTextInternal(widened, legacyCoords, originalRasterWidth, colorIndex);
}

bool HighResFontOverlay::enqueueText(const Common::U32String &text, const Common::Point &legacyCoords,
		int originalRasterWidth, uint32 colorIndex) {
	return enqueueTextInternal(text, legacyCoords, originalRasterWidth, colorIndex);
}

bool HighResFontOverlay::enqueueTextInternal(const Common::U32String &text, const Common::Point &legacyCoords,
		int originalRasterWidth, uint32 colorIndex) {
	if (!isActive() || text.empty())
		return false;

	Common::StackLock lock(_queueMutex);

	// Watchdog: something downstream failed to call renderOverlay() and
	// flush the queue (e.g. the backend stopped presenting frames). Drop
	// the backlog rather than growing without bound, and tell the caller
	// to use the raster path this frame.
	if (_renderQueue.size() >= kMaxQueueDepth) {
		warning("HighResFontOverlay: render queue exceeded %u entries without being flushed; "
			"dropping backlog and falling back to raster rendering", (uint)kMaxQueueDepth);
		_renderQueue.clear();
		return false;
	}

	QueuedText entry;
	entry.text = text;
	entry.coords = legacyCoords;
	entry.targetWidth = originalRasterWidth;
	entry.color = colorIndex;
	_renderQueue.push_back(entry);
	return true;
}

void HighResFontOverlay::clearQueue() {
	Common::StackLock lock(_queueMutex);
	_renderQueue.clear();
}

void HighResFontOverlay::flushGlyphCache() {
#ifdef USE_FREETYPE2
	if (_atlas)
		_atlas->flush();
#endif
}

void HighResFontOverlay::renderQueuedText(const QueuedText &entry, Graphics::Surface *dst) {
#ifdef USE_FREETYPE2
	if (!_font || !_atlas || entry.text.empty())
		return;

	// colorIndex is expected to already be resolved by the caller (who
	// alone knows how to map a legacy palette index to RGB) into a
	// packed 0x00RRGGBB triple -- core overlay code has no business
	// reaching into an engine's active palette.
	const uint8 r = (uint8)((entry.color >> 16) & 0xFF);
	const uint8 g = (uint8)((entry.color >> 8) & 0xFF);
	const uint8 b = (uint8)(entry.color & 0xFF);

	// legacy_w: what the original raster font would have occupied,
	// scaled to native resolution. A negative or otherwise nonsensical
	// caller-supplied width is clamped to zero rather than propagated,
	// since it would otherwise flip the sign of the kerning adjustment
	// below in a way no legitimate caller intends.
	const float legacyWidthNative = MAX(entry.targetWidth, 0) * _scaleX;

	// vector_w: the natural width of the same run set in the vector
	// font, plus the character count needed for the kerning divisor.
	int vectorWidth = 0;
	uint32 numChars = 0;
	for (uint i = 0; i < entry.text.size(); i++) {
		uint32 chr = entry.text[i];
		vectorWidth += _font->getCharWidth(chr);
		numChars++;
	}

	if (numChars == 0)
		return;

	float dynamicKerning = 0.0f;
	if (numChars > 1)
		dynamicKerning = (legacyWidthNative - (float)vectorWidth) / (float)(numChars - 1);

	const Common::Point origin = scaleCoordinatesToNative(entry.coords);
	const int clipRight = origin.x + (int)floor(legacyWidthNative + 0.5f);

	float penX = (float)origin.x;

	for (uint i = 0; i < entry.text.size(); i++) {
		uint32 chr = entry.text[i];

		GlyphInfo glyph;
		if (!_atlas->getGlyph(chr, _font.get(), glyph)) {
			debug(5, "HighResFontOverlay: glyph %u could not be cached, skipping", chr);
			penX += dynamicKerning;
			continue;
		}

		// Sub-pixel anti-jitter: always land on an integer pixel
		// column, so text does not shimmer as the camera pans.
		int blitX = (int)floor(penX + glyph.bearingX + 0.5f);
		int blitY = (int)floor(origin.y + glyph.bearingY + 0.5f);

		const byte *atlasPixels = _atlas->getPixels();
		const uint16 atlasWidth = _atlas->getWidth();

		for (uint16 gy = 0; gy < glyph.height; gy++) {
			int dstY = blitY + gy;
			for (uint16 gx = 0; gx < glyph.width; gx++) {
				int dstX = blitX + gx;
				// Strict adherence to the original bounding box: never
				// bleed past the width the legacy layout allotted.
				if (dstX >= clipRight)
					break;
				uint8 coverage = atlasPixels[(size_t)(glyph.v + gy) * atlasWidth + (glyph.u + gx)];
				blendCoverageSample(dst, dstX, dstY, coverage, r, g, b);
			}
		}

		penX += glyph.advance + dynamicKerning;
	}
#endif
}

void HighResFontOverlay::renderOverlay(Graphics::Surface *nativeScreenSurface) {
	if (!nativeScreenSurface)
		return;

	Common::Array<QueuedText> queue;
	{
		Common::StackLock lock(_queueMutex);
		if (_renderQueue.empty())
			return;
		queue = _renderQueue;
		_renderQueue.clear();
	}

	if (!isActive())
		return;

	for (uint i = 0; i < queue.size(); i++)
		renderQueuedText(queue[i], nativeScreenSurface);
}

} // End of namespace Graphics
