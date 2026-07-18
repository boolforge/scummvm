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

#include "graphics/fonts/glyph_atlas.h"

#ifdef USE_FREETYPE2

#include "graphics/font.h"
#include "graphics/surface.h"
#include "graphics/pixelformat.h"
#include "common/rect.h"
#include "common/debug.h"

namespace Graphics {

GlyphAtlas::GlyphAtlas(uint16 width, uint16 height) : _width(width), _height(height) {
	// One byte per pixel: an 8-bit anti-aliased coverage/alpha mask.
	// A 512x512 atlas costs 256KB of RAM, well under the ~1MB ceiling
	// appropriate for constrained platforms (Nintendo DS, PSVita, etc.)
	// while comfortably holding a full working glyph set.
	_pixels = (byte *)calloc((size_t)_width * _height, 1);
}

GlyphAtlas::~GlyphAtlas() {
	free(_pixels);
}

void GlyphAtlas::flush() {
	_shelves.clear();
	_cache.clear();
	if (_pixels)
		memset(_pixels, 0, (size_t)_width * _height);
	debug(3, "GlyphAtlas: cache flushed (room/scene transition)");
}

bool GlyphAtlas::allocateShelf(uint16 w, uint16 h, uint16 &outU, uint16 &outV) {
	if (w > _width || h > _height)
		return false;

	// First-fit: reuse an existing shelf tall enough for this glyph if
	// there is horizontal room left on it.
	for (uint i = 0; i < _shelves.size(); i++) {
		Shelf &shelf = _shelves[i];
		if (h <= shelf.height && shelf.nextX + w <= _width) {
			outU = shelf.nextX;
			outV = shelf.y;
			shelf.nextX += w;
			return true;
		}
	}

	// No existing shelf fits; start a new one at the current bottom of
	// the atlas, sized to this glyph's height.
	uint16 bottom = 0;
	for (uint i = 0; i < _shelves.size(); i++)
		bottom += _shelves[i].height;

	if (bottom + h > _height)
		return false;

	Shelf shelf;
	shelf.y = bottom;
	shelf.height = h;
	shelf.nextX = w;
	_shelves.push_back(shelf);

	outU = 0;
	outV = bottom;
	return true;
}

bool GlyphAtlas::rasterizeAndPack(uint32 chr, const Font *font, GlyphInfo &info) {
	Common::Rect bbox = font->getBoundingBox(chr);
	uint16 w = (uint16)MAX<int>(bbox.width(), 1);
	uint16 h = (uint16)MAX<int>(bbox.height(), 1);

	uint16 u, v;
	if (!allocateShelf(w, h, u, v)) {
		// Atlas full: try recovering by flushing once before giving up.
		// This is the same "room transition" bound-memory strategy
		// applied opportunistically, rather than only on an explicit
		// engine-driven scene change.
		flush();
		if (!allocateShelf(w, h, u, v)) {
			debug(5, "GlyphAtlas: unable to pack glyph %u (%dx%d does not fit %dx%d atlas)",
				chr, w, h, _width, _height);
			return false;
		}
	}

	// Rasterize the glyph in isolation via the font's own public
	// drawChar(), rather than talking to FreeType directly: draw fully
	// opaque white ink on a fully transparent 32-bit surface. Because
	// Font::drawChar() alpha-composites source coverage against the
	// destination, the resulting alpha channel *is* the anti-aliased
	// coverage mask FreeType produced -- with no need to duplicate
	// FreeType face/library lifetime management outside of ttf.cpp.
	Graphics::Surface glyphSurf;
	Graphics::PixelFormat argb = Graphics::PixelFormat::createFormatARGB32();
	glyphSurf.create(w, h, argb);

	font->drawChar(&glyphSurf, chr, -bbox.left, -bbox.top, argb.ARGBToColor(255, 255, 255, 255));

	for (uint16 y = 0; y < h; y++) {
		byte *dst = _pixels + (size_t)(v + y) * _width + u;
		for (uint16 x = 0; x < w; x++) {
			uint32 pixel = glyphSurf.getPixel(x, y);
			uint8 a, r, g, b;
			argb.colorToARGB(pixel, a, r, g, b);
			dst[x] = a;
		}
	}

	glyphSurf.free();

	info.u = u;
	info.v = v;
	info.width = w;
	info.height = h;
	info.bearingX = (int16)bbox.left;
	info.bearingY = (int16)bbox.top;
	info.advance = (uint16)MAX<int>(font->getCharWidth(chr), 0);

	_cache[chr] = info;
	return true;
}

bool GlyphAtlas::getGlyph(uint32 chr, const Font *font, GlyphInfo &info) {
	Common::HashMap<uint32, GlyphInfo>::iterator it = _cache.find(chr);
	if (it != _cache.end()) {
		info = it->_value;
		return true;
	}

	return rasterizeAndPack(chr, font, info);
}

} // End of namespace Graphics

#endif // USE_FREETYPE2
