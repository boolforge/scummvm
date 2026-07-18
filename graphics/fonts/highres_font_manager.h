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

#ifndef GRAPHICS_FONTS_HIGHRES_FONT_MANAGER_H
#define GRAPHICS_FONTS_HIGHRES_FONT_MANAGER_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/hashmap.h"
#include "common/hash-str.h"
#include "common/ptr.h"
#include "common/singleton.h"

namespace Graphics {

class HighResFontOverlay;
class Surface;

/**
 * @defgroup graphics_fonts_highres_manager High-resolution font manager
 * @ingroup graphics
 *
 * @brief HighResFontManager class: a keyed registry of HighResFontOverlay
 * instances, one per distinct engine/subsystem font request.
 *
 * @{
 */

/**
 * Engine-agnostic registry that owns one HighResFontOverlay per
 * requested font "id" (e.g. "scumm_dialogue", "bladerunner_subtitles"),
 * so unrelated engines/subsystems can each request a high-resolution
 * font without clobbering each other's state, while backends only need
 * a single call (renderActiveOverlays()) to composite everything that
 * is currently active -- they do not need to know which engines or how
 * many overlays exist.
 *
 * This class intentionally has no dependency on Common::ConfigManager:
 * deciding *whether* to request/use a high-resolution font (and under
 * which config key) is an engine-level policy choice, not something
 * this reusable core module should hardcode.
 */
class HighResFontManager : public Common::Singleton<HighResFontManager> {
public:
	/** Parameters for a single named high-resolution font request. */
	struct FontConfig {
		Common::String path;
		int pointSize = 0;
		Common::String id;
	};

	/**
	 * Returns the overlay registered for 'config.id', loading it first
	 * if this is the first request for that id. Repeated calls with
	 * the same id are cheap lookups, not reloads.
	 *
	 * @return A non-owning pointer to the overlay (the manager retains
	 *         ownership; the returned overlay stays valid for the
	 *         lifetime of the process/manager). Never null -- even if
	 *         font loading failed, an inactive HighResFontOverlay is
	 *         returned so callers can uniformly check isActive().
	 */
	HighResFontOverlay *loadFont(const FontConfig &config);

	/** Looks up a previously loaded overlay by id, or nullptr if none was requested. */
	HighResFontOverlay *getOverlay(const Common::String &id) const;

	/** Renders every registered overlay's queued text onto 'dst' in one call. */
	void renderActiveOverlays(Graphics::Surface *dst);

	/** Clears every registered overlay's pending (not yet rendered) queue. */
	void clearAllQueues();

	/** Flushes every registered overlay's glyph cache (room/scene transition). */
	void flushAllGlyphCaches();

private:
	friend class Common::Singleton<SingletonBaseType>;
	HighResFontManager();
	~HighResFontManager();

	Common::HashMap<Common::String, Common::SharedPtr<HighResFontOverlay> > _overlays;
};

/** @} */

} // End of namespace Graphics

/** Shortcut for accessing the high-resolution font manager. */
#define HighResFontMan (Graphics::HighResFontManager::instance())

#endif
