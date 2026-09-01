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

#include "glk/quill/detection.h"
#include "glk/quill/detection_tables.h"
#include "glk/quill/database.h"
#include "common/file.h"
#include "common/md5.h"
#include "engines/game.h"

namespace Glk {
namespace Quill {

void QuillMetaEngine::getSupportedGames(PlainGameList &games) {
	for (const PlainGameDescriptor *pd = QUILL_GAME_LIST; pd->gameId; ++pd)
		games.push_back(*pd);
}

GameDescriptor QuillMetaEngine::findGame(const char *gameId) {
	for (const PlainGameDescriptor *pd = QUILL_GAME_LIST; pd->gameId; ++pd) {
		if (!strcmp(gameId, pd->gameId)) {
			GameDescriptor gd = *pd;
			gd._supportLevel = kUnstableGame;
			return gd;
		}
	}

	return GameDescriptor::empty();
}

bool QuillMetaEngine::detectGames(const Common::FSList &fslist, DetectedGames &gameList) {
	for (Common::FSList::const_iterator file = fslist.begin(); file != fslist.end(); ++file) {
		if (file->isDirectory())
			continue;

		Common::String filename = file->getName();
		if (!filename.hasSuffixIgnoreCase(".xex"))
			continue;

		Common::File gameFile;
		if (!gameFile.open(*file))
			continue;

		bool matches = QuillDatabase::quickDetect(&gameFile);
		gameFile.close();

		if (matches) {
			GameDescriptor gameDesc = findGame("quillgame");
			GlkDetectedGame gd("quillgame", gameDesc.description, filename);
			gameList.push_back(gd);
		}
	}

	return !gameList.empty();
}

void QuillMetaEngine::detectClashes(Common::StringMap &map) {
	for (const PlainGameDescriptor *pd = QUILL_GAME_LIST; pd->gameId; ++pd) {
		if (map.contains(pd->gameId))
			error("Duplicate game Id found - %s", pd->gameId);
		map[pd->gameId] = "";
	}
}

const GlkDetectionEntry *QuillMetaEngine::getDetectionEntries() {
	static Common::Array<GlkDetectionEntry> entries;

	// Only real, MD5-verified per-title entries feed the generic
	// AdvancedDetector-style table; the "quillgame" structural fallback is
	// handled directly in detectGames() above instead, since it has no
	// fixed filename or hash to offer here.
	for (const QuillDetectionEntry *entry = QUILL_GAMES; entry->_gameId; ++entry) {
		if (!entry->_filename || !entry->_md5)
			continue;

		GlkDetectionEntry detection = {
			entry->_gameId,
			entry->_filename,
			entry->_md5,
			0,
			Common::EN_ANY,
			Common::kPlatformAtari8Bit
		};
		entries.push_back(detection);
	}

	entries.push_back({nullptr,
					   nullptr,
					   nullptr,
					   0,
					   Common::UNK_LANG,
					   Common::kPlatformUnknown});

	return entries.data();
}

} // End of namespace Quill
} // End of namespace Glk
