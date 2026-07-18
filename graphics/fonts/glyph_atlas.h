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

#ifndef GRAPHICS_FONTS_GLYPH_ATLAS_H
#define GRAPHICS_FONTS_GLYPH_ATLAS_H

#include "common/scummsys.h"

#ifdef USE_FREETYPE2

#include "common/hashmap.h"
#include "common/array.h"
#include "common/noncopyable.h"

namespace Graphics {

class Font;

/**
 * @defgroup graphics_fonts_glyph_atlas High-resolution font glyph atlas
 * @ingroup graphics
 *
 * @brief GlyphAtlas class used to cache rasterized vector glyphs.
 *
 * @{
 */

/**
 * Metrics and atlas placement for a single cached glyph, as used by the
 * HighResFontOverlay pipeline described in the font modernization RFC.
 *
 * This intentionally mirrors the layout of the glyph registry lookup
 * table from the RFC: u/v/width/height locate the glyph inside the
 * shared 8-bit alpha texture, while bearingX/bearingY/advance are the
 * metrics required to position and advance the pen correctly.
 */
struct GlyphInfo {
	uint16 u = 0;          ///< X coordinate of the glyph's top-left pixel in the atlas.
	uint16 v = 0;           ///< Y coordinate of the glyph's top-left pixel in the atlas.
	uint16 width = 0;       ///< Width in pixels of the cached glyph.
	uint16 height = 0;      ///< Height in pixels of the cached glyph.
	int16 bearingX = 0;     ///< Horizontal offset from the pen to the glyph's left edge.
	int16 bearingY = 0;     ///< Vertical offset from the drawing origin to the glyph's top edge.
	uint16 advance = 0;     ///< Horizontal distance to move the pen after drawing this glyph.
};

/**
 * A memory-constrained, shelf-packed 8-bit alpha texture atlas used to
 * cache rasterized glyphs for the high-resolution font overlay.
 *
 * Only a single byte per pixel is stored (an anti-aliased coverage
 * value), rather than a fully colored 32-bit glyph, since the active
 * palette color is applied dynamically at composition time. This keeps
 * the memory footprint small enough to be safe on constrained platforms
 * (e.g. a 512x512 atlas costs 256KB of RAM).
 *
 * Glyphs are rasterized lazily, the first time they are requested, via
 * the owning Font's own public drawChar() API rather than by talking to
 * FreeType directly: a glyph is drawn in white on a fully transparent
 * 32-bit surface, and the resulting alpha channel is exactly the
 * anti-aliased coverage mask FreeType produced, without ScummVM's font
 * modernization code needing to duplicate FreeType face/library
 * lifetime management. See rasterizeAndPack() for details.
 *
 * The cache is flushed via flush(), which callers are expected to
 * invoke on room/scene transitions -- adventure games naturally divide
 * dialogue by room, so this bounds memory use without needing
 * per-glyph usage tracking.
 */
class GlyphAtlas : public Common::NonCopyable {
public:
	/**
	 * Creates a new glyph atlas.
	 *
	 * @param width  Width in pixels of the backing texture. Default of
	 *               512 keeps memory use low (256KB at height=512)
	 *               while comfortably fitting a full Latin-1 glyph set
	 *               at typical subtitle point sizes.
	 * @param height Height in pixels of the backing texture.
	 */
	GlyphAtlas(uint16 width = 512, uint16 height = 512);
	~GlyphAtlas();

	/**
	 * Looks up (or rasterizes and caches, on a miss) the glyph for
	 * 'chr' as rendered by 'font'.
	 *
	 * @param chr  The Unicode codepoint to look up.
	 * @param font The font to rasterize with on a cache miss. Must
	 *             remain valid for the lifetime of any GlyphInfo
	 *             returned, since bearing/advance values are only
	 *             meaningful in the context of that font.
	 * @param info Filled in with the glyph's metrics and atlas
	 *             position on success.
	 * @return true if the glyph is available (from cache or freshly
	 *         rasterized), false if it could not be packed (e.g. the
	 *         atlas is full even after a flush, or the font could not
	 *         render the codepoint). Callers must treat false as a
	 *         signal to fall back to the legacy raster rendering path.
	 */
	bool getGlyph(uint32 chr, const Font *font, GlyphInfo &info);

	/**
	 * Discards every cached glyph and resets the shelf packer, without
	 * reallocating the backing texture. Intended to be called on room
	 * transitions per the RFC's eviction strategy.
	 */
	void flush();

	uint16 getWidth() const { return _width; }
	uint16 getHeight() const { return _height; }

	/** Raw 8-bit alpha pixel data, 'getWidth() * getHeight()' bytes, row-major. */
	const byte *getPixels() const { return _pixels; }

private:
	struct Shelf {
		uint16 y;
		uint16 height;
		uint16 nextX;
	};

	bool allocateShelf(uint16 w, uint16 h, uint16 &outU, uint16 &outV);
	bool rasterizeAndPack(uint32 chr, const Font *font, GlyphInfo &info);

	uint16 _width;
	uint16 _height;
	byte *_pixels;

	Common::Array<Shelf> _shelves;
	Common::HashMap<uint32, GlyphInfo> _cache;
};

/** @} */

} // End of namespace Graphics

#endif // USE_FREETYPE2

#endif
