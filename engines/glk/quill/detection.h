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

#ifndef GLK_QUILL_DETECTION_H
#define GLK_QUILL_DETECTION_H

#include "engines/game.h"
#include "common/fs.h"
#include "common/hash-str.h"
#include "glk/detection.h"

namespace Glk {
namespace Quill {

/**
 * Atari AdventureWriter/Quill games generally aren't individually catalogued
 * by filename+MD5 the way most ScummVM detection tables are - unlike most
 * IF systems, the interpreter and database ship together in one .xex per
 * release, and different hobbyist releases are rarely archived with stable,
 * well-known checksums. Detection instead verifies the *interpreter* segment
 * against a known-good signature (see QuillDatabase::quickDetect), which
 * reliably identifies "a database this engine can run", regardless of which
 * specific adventure it is. Specific, MD5-verified titles can still be added
 * to QUILL_GAMES/QUILL_GAME_LIST as real dumps become available.
 */
class QuillMetaEngine {
public:
	static void getSupportedGames(PlainGameList &games);
	static GameDescriptor findGame(const char *gameId);
	static bool detectGames(const Common::FSList &fslist, DetectedGames &gameList);
	static void detectClashes(Common::StringMap &map);
	static const GlkDetectionEntry *getDetectionEntries();
};

} // End of namespace Quill
} // End of namespace Glk

#endif
