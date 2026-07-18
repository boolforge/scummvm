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

#include "graphics/fonts/highres_font_manager.h"
#include "graphics/fonts/highres_font_overlay.h"

namespace Common {
DECLARE_SINGLETON(Graphics::HighResFontManager);
}

namespace Graphics {

HighResFontManager::HighResFontManager() {
}

HighResFontManager::~HighResFontManager() {
}

HighResFontOverlay *HighResFontManager::loadFont(const FontConfig &config) {
	Common::HashMap<Common::String, Common::SharedPtr<HighResFontOverlay> >::iterator it = _overlays.find(config.id);
	if (it != _overlays.end())
		return it->_value.get();

	Common::SharedPtr<HighResFontOverlay> overlay(new HighResFontOverlay());
	overlay->loadTrueTypeFont(config.path, config.pointSize);
	_overlays[config.id] = overlay;
	return overlay.get();
}

HighResFontOverlay *HighResFontManager::getOverlay(const Common::String &id) const {
	Common::HashMap<Common::String, Common::SharedPtr<HighResFontOverlay> >::const_iterator it = _overlays.find(id);
	if (it == _overlays.end())
		return nullptr;
	return it->_value.get();
}

void HighResFontManager::renderActiveOverlays(Graphics::Surface *dst) {
	for (Common::HashMap<Common::String, Common::SharedPtr<HighResFontOverlay> >::iterator it = _overlays.begin();
			it != _overlays.end(); ++it) {
		if (it->_value && it->_value->isActive())
			it->_value->renderOverlay(dst);
	}
}

void HighResFontManager::clearAllQueues() {
	for (Common::HashMap<Common::String, Common::SharedPtr<HighResFontOverlay> >::iterator it = _overlays.begin();
			it != _overlays.end(); ++it) {
		if (it->_value)
			it->_value->clearQueue();
	}
}

void HighResFontManager::flushAllGlyphCaches() {
	for (Common::HashMap<Common::String, Common::SharedPtr<HighResFontOverlay> >::iterator it = _overlays.begin();
			it != _overlays.end(); ++it) {
		if (it->_value)
			it->_value->flushGlyphCache();
	}
}

} // End of namespace Graphics
