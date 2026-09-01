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

#include "common/language.h"
#include "engines/game.h"

namespace Glk {
namespace Quill {

/**
 * "quillgame" is a generic, structurally-detected entry (see detection.cpp)
 * matching any Atari AdventureWriter/Quill database paired with the known
 * interpreter, since we don't yet have MD5-verified dumps of specific
 * commercial/hobbyist releases to list individually. At least 36 distinct
 * AdventureWriter Atari titles are known to exist (per
 * https://intfiction.org/t/adventurewriter-games-for-atari-8-bit-computers/77646
 * and cross-referenced against atarimania.com), several confirmed
 * Freeware/PD (e.g. Mission Moon, California Gold, Saucer Adventure).
 * As real dumps are obtained and verified (see
 * .github/workflows/quill-validate.yml), add per-title entries here with
 * real filename+MD5 pairs - never fabricate a hash.
 */
const PlainGameDescriptor QUILL_GAME_LIST[] = {
	{"quillgame", "Unknown Quill/AdventureWriter Game"},

	{nullptr, nullptr}
};

struct QuillDetectionEntry {
	const char *const _gameId;
	const char *const _filename; ///< nullptr - matched structurally instead, see detectGames()
	const char *const _md5;      ///< nullptr - not applicable to the structural entry
};

const QuillDetectionEntry QUILL_GAMES[] = {
	{ "quillgame", nullptr, nullptr },

	{ nullptr, nullptr, nullptr }
};

} // End of namespace Quill
} // End of namespace Glk
