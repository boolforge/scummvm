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

#ifndef GLK_QUILL_VM_H
#define GLK_QUILL_VM_H

#include "common/random.h"
#include "common/str.h"
#include "common/array.h"
#include "glk/quill/quill_types.h"
#include "glk/quill/database.h"

namespace Glk {
namespace Quill {

/**
 * Callback interface the VM uses to talk to the outside world (screen,
 * keyboard, save/restore), so this file stays free of any direct Glk
 * dependency - the Quill engine class implements this against real Glk
 * windows/events.
 */
class QuillIOHandler {
public:
	virtual ~QuillIOHandler() {}

	/// Print text, interpreting QuillDatabase::INVERSE_ON/OFF as style toggles.
	virtual void print(const Common::String &text) = 0;
	virtual void newLine() = 0;

	/// Block until the player submits a line; returned text is used as-is (VM uppercases it).
	virtual Common::String readLine() = 0;
	virtual void waitKey() = 0;

	virtual void clearScreen() = 0;
	virtual void setColors(byte textLuminance, byte background, byte border) = 0;
	virtual void sound(byte voice, byte pitch, byte distortion, byte volume) = 0;

	/// Delegate to the ScummVM save/restore GUI (via GlkEngine::saveGame/loadGame).
	/// Named differently from GlkEngine's own saveGame()/loadGame() to avoid an
	/// ambiguous-base-member clash in the engine class, which inherits both
	/// GlkAPI (-> GlkEngine) and this interface. Returns success.
	virtual bool doSaveGame() = 0;
	virtual bool doLoadGame() = 0;

	/// True once the engine's own shouldQuit()/event-loop wants us to stop (window closed, etc).
	virtual bool shouldQuit() const = 0;
};

/// What running one condact's condition block decided.
struct CondactResult {
	bool conditionsPassed = false;
	ActionResult actionResult = ActionResult::Continue;
};

/**
 * Executes a Quill/AdventureWriter Atari game against a loaded QuillDatabase,
 * following the same overall control-flow graph as the original 6502
 * interpreter (see quill_types.h's ActionResult for the distinct "exit
 * routes" out of action processing that the original code jumps between).
 */
class QuillVM {
public:
	static const int FLAG_COUNT = 37;

	QuillVM(QuillDatabase &db, QuillIOHandler &io);

	/// Runs until the game is quit for good (SystemReset) or the IO handler wants to stop.
	void run();

	// --- State access, used by the engine class for Quetzal save-chunk serialization ---
	byte *flagsPtr() { return _flags; }
	const byte *flagsPtr() const { return _flags; }
	Common::Array<byte> &objLoc() { return _objLoc; }
	const Common::Array<byte> &objLoc() const { return _objLoc; }

private:
	QuillDatabase &_db;
	QuillIOHandler &_io;
	byte _flags[FLAG_COUNT];
	Common::Array<byte> _objLoc;      ///< One entry per object: location, or a QLOC_* special value
	Common::RandomSource _rnd;

	// Flags 30-36 have fixed, interpreter-assigned meanings (this mirrors the
	// original's own memory layout, where these are just specific offsets
	// into the same 37-byte flag array everything else lives in).
	byte &objectsCarriedCount() { return _flags[1]; }
	byte &score()               { return _flags[30]; }
	byte &turnsLSB()             { return _flags[31]; }
	byte &turnsMSB()             { return _flags[32]; }
	byte &word1()                { return _flags[33]; }
	byte &word2()                { return _flags[34]; }
	byte &currentLocation()      { return _flags[35]; }
	// Flag 0 is the authoring convention (standard across all Quill
	// platforms) for "current location is dark".
	byte &darkFlag()             { return _flags[0]; }

	byte objectLocation(byte obj) const {
		return (obj < _objLoc.size()) ? _objLoc[obj] : QLOC_NOTCREATED;
	}
	void setObjectLocation(byte obj, byte loc) {
		if (obj < _objLoc.size()) _objLoc[obj] = loc;
	}

	void initialiseGame();
	void describeLocation();
	ActionResult doTurn();
	ActionResult processEvents();

	Common::String readAndParseLine();
	void parseWords(const Common::String &line);
	byte matchWord(const Common::String &word) const;

	CondactResult executeCondact(uint16 offset);
	bool evaluateConditions(uint16 &offset);
	ActionResult executeActions(uint16 &offset);

	bool evalCondition(ConditionOpcode op, byte arg, byte constant);
	ActionResult execAction(ActionOpcode op, byte arg, byte a1, byte a2, byte a3);

	Common::String sysMsg(SystemMessageId id) const { return _db.systemMessageText(id); }
	static int extraArgsForCondition(ConditionOpcode op);
	static int extraArgsForAction(ActionOpcode op);
};

} // End of namespace Quill
} // End of namespace Glk

#endif
