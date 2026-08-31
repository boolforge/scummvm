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

#include "glk/quill/vm.h"
#include "common/algorithm.h"
#include "common/tokenizer.h"

namespace Glk {
namespace Quill {

QuillVM::QuillVM(QuillDatabase &db, QuillIOHandler &io) : _db(db), _io(io), _rnd("quill") {
	memset(_flags, 0, sizeof(_flags));
}

void QuillVM::initialiseGame() {
	memset(_flags, 0, sizeof(_flags));

	_objLoc.clear();
	const Common::Array<byte> &starts = _db.objectStartLocations();
	for (uint i = 0; i < _db.header().objectCount; ++i)
		_objLoc.push_back(i < starts.size() ? starts[i] : QLOC_NOTCREATED);

	currentLocation() = 1; // Games always start at location 1
	_io.setColors(_db.header().color1, _db.header().color2, _db.header().color4);
	_io.clearScreen();
}

void QuillVM::describeLocation() {
	if (darkFlag() != 0) {
		_io.print(sysMsg(SM_DARK));
		_io.newLine();
		return;
	}

	_io.print(_db.locationText(currentLocation()));
	_io.newLine();

	bool first = true;
	for (uint obj = 0; obj < _db.header().objectCount; ++obj) {
		if (objectLocation(obj) != currentLocation())
			continue;
		if (first) {
			_io.print(sysMsg(SM_ALSO_SEE));
			_io.newLine();
			first = false;
		}
		_io.print(_db.objectText(obj));
		_io.newLine();
	}
}

byte QuillVM::matchWord(const Common::String &word) const {
	Common::String w = word;
	w.toUppercase();
	if (w.size() > 4)
		w = Common::String(w.c_str(), 4);

	for (uint i = 0; i < _db.vocabulary().size(); ++i) {
		Common::String v = _db.vocabulary()[i].text;
		if (v.size() > 4)
			v = Common::String(v.c_str(), 4);
		if (v.equalsIgnoreCase(w))
			return _db.vocabulary()[i].number;
	}
	return QWORD_WILDCARD;
}

void QuillVM::parseWords(const Common::String &line) {
	Common::StringTokenizer tok(line, " \t");
	Common::String w1 = tok.nextToken();
	Common::String w2 = tok.nextToken();

	word1() = w1.empty() ? 0 : matchWord(w1);
	word2() = w2.empty() ? 0 : matchWord(w2);
}

Common::String QuillVM::readAndParseLine() {
	_io.print(sysMsg(SM_PROMPT1));
	return _io.readLine();
}

bool QuillVM::evaluateConditions(uint16 &offset) {
	for (;;) {
		byte op = _db.byteAt(offset);
		if (op == QTAB_END) {
			offset++;
			return true;
		}
		offset++;
		byte arg = _db.byteAt(offset++);
		byte constant = 0;
		if (op == COP_EQ || op == COP_GT || op == COP_LT)
			constant = _db.byteAt(offset++);

		if (op >= COP_COUNT || !evalCondition(ConditionOpcode(op), arg, constant))
			return false;
	}
}

ActionResult QuillVM::executeActions(uint16 &offset) {
	for (;;) {
		byte op = _db.byteAt(offset);
		if (op == QTAB_END) {
			offset++;
			return ActionResult::Continue;
		}
		offset++;
		byte arg = _db.byteAt(offset++);

		byte a1 = 0, a2 = 0, a3 = 0;
		if (op < AOP_COUNT) {
			int extra = extraArgsForAction(ActionOpcode(op));
			if (extra >= 1) a1 = _db.byteAt(offset++);
			if (extra >= 2) a2 = _db.byteAt(offset++);
			if (extra >= 3) a3 = _db.byteAt(offset++);
		}

		if (op >= AOP_COUNT)
			continue; // Unknown opcode - skip rather than crash on a corrupt/unsupported database

		ActionResult r = execAction(ActionOpcode(op), arg, a1, a2, a3);
		if (r != ActionResult::Continue)
			return r;
	}
}

CondactResult QuillVM::executeCondact(uint16 offset) {
	CondactResult r;
	r.conditionsPassed = evaluateConditions(offset);
	if (r.conditionsPassed)
		r.actionResult = executeActions(offset);
	return r;
}

ActionResult QuillVM::processEvents() {
	for (uint i = 0; i < _db.events().size(); ++i) {
		const QuillEventEntry &e = _db.events()[i];
		bool w1ok = (e.word1 == QWORD_WILDCARD) || (e.word1 == word1());
		bool w2ok = (e.word2 == QWORD_WILDCARD) || (e.word2 == word2());
		if (!w1ok || !w2ok)
			continue;

		CondactResult cr = executeCondact(e.condactAddr);
		if (cr.conditionsPassed)
			return cr.actionResult; // Committed to this entry - stop scanning either way
		// Conditions failed: per standard Quill semantics, keep looking for
		// another entry matching the same word pair before giving up.
	}

	_io.print((word1() < FIRST_NON_DIRECTION_WORD) ? sysMsg(SM_CANT_GO) : sysMsg(SM_CANT_DO));
	_io.newLine();
	return ActionResult::Done;
}

ActionResult QuillVM::doTurn() {
	// STATUS table: evaluated unconditionally every turn, in order. Each
	// entry's own condition opcodes are its real gate.
	for (uint i = 0; i < _db.statuses().size(); ++i) {
		CondactResult cr = executeCondact(_db.statuses()[i].condactAddr);
		if (cr.conditionsPassed && cr.actionResult != ActionResult::Continue)
			return cr.actionResult;
	}

	uint16 turns = (turnsMSB() << 8) | turnsLSB();
	turns++;
	turnsLSB() = turns & 0xFF;
	turnsMSB() = (turns >> 8) & 0xFF;

	Common::String line = readAndParseLine();
	parseWords(line);

	if (word1() == QWORD_WILDCARD) {
		_io.print(sysMsg(SM_DONT_UNDERSTAND));
		_io.newLine();
		return ActionResult::Done;
	}

	const Common::Array<QuillConnection> &conns = _db.connectionsForLocation(currentLocation());
	for (uint i = 0; i < conns.size(); ++i) {
		if (conns[i].word == word1()) {
			currentLocation() = conns[i].targetLocation;
			return ActionResult::MainGameLoop;
		}
	}

	return processEvents();
}

void QuillVM::run() {
	initialiseGame();
	describeLocation();

	while (!_io.shouldQuit()) {
		ActionResult r = doTurn();
		switch (r) {
		case ActionResult::MainGameLoop:
			describeLocation();
			break;
		case ActionResult::InitialiseGame:
			initialiseGame();
			describeLocation();
			break;
		case ActionResult::SystemReset:
			return;
		case ActionResult::Done:
		case ActionResult::Continue:
		default:
			break; // Straight back around to STATUS + prompt for the next command
		}
	}
}

int QuillVM::extraArgsForCondition(ConditionOpcode op) {
	switch (op) {
	case COP_EQ: case COP_GT: case COP_LT:
		return 1;
	default:
		return 0;
	}
}

int QuillVM::extraArgsForAction(ActionOpcode op) {
	switch (op) {
	case AOP_SWAP: case AOP_PLACE:
	case AOP_PLUS: case AOP_MINUS: case AOP_LET:
		return 1;
	case AOP_SOUND:
		return 3;
	default:
		return 0;
	}
}

bool QuillVM::evalCondition(ConditionOpcode op, byte arg, byte constant) {
	switch (op) {
	case COP_AT:      return currentLocation() == arg;
	case COP_NOTAT:   return currentLocation() != arg;
	case COP_ATGT:    return currentLocation() > arg;
	case COP_ATLT:    return currentLocation() < arg;
	case COP_PRESENT: {
		byte loc = objectLocation(arg);
		return loc == currentLocation() || loc == QLOC_CARRIED || loc == QLOC_WORN;
	}
	case COP_ABSENT: {
		byte loc = objectLocation(arg);
		return !(loc == currentLocation() || loc == QLOC_CARRIED || loc == QLOC_WORN);
	}
	case COP_WORN:    return objectLocation(arg) == QLOC_WORN;
	case COP_NOTWORN: return objectLocation(arg) != QLOC_WORN;
	case COP_CARRIED: return objectLocation(arg) == QLOC_CARRIED;
	case COP_NOTCARR: return objectLocation(arg) != QLOC_CARRIED;
	case COP_CHANCE:  return byte(_rnd.getRandomNumber(99) + 1) <= arg;
	case COP_ZERO:    return _flags[arg] == 0;
	case COP_NOTZERO: return _flags[arg] != 0;
	case COP_EQ:      return _flags[arg] == constant;
	case COP_GT:      return _flags[arg] > constant;
	case COP_LT:      return _flags[arg] < constant;
	default:          return false;
	}
}

ActionResult QuillVM::execAction(ActionOpcode op, byte arg, byte a1, byte a2, byte a3) {
	switch (op) {
	case AOP_INV: {
		bool any = false;
		for (uint obj = 0; obj < _db.header().objectCount; ++obj) {
			byte loc = objectLocation(obj);
			if (loc != QLOC_CARRIED && loc != QLOC_WORN)
				continue;
			if (!any) {
				_io.print(sysMsg(SM_HAVE_WITH_ME));
				_io.newLine();
				any = true;
			}
			_io.print(_db.objectText(obj));
			if (loc == QLOC_WORN) {
				_io.print(" (");
				_io.print(sysMsg(SM_WORN));
				_io.print(")");
			}
			_io.newLine();
		}
		if (!any) {
			_io.print(sysMsg(SM_NOTHING));
			_io.newLine();
		}
		return ActionResult::Done;
	}

	case AOP_DESC:
		return ActionResult::MainGameLoop;

	case AOP_QUIT: {
		_io.print(sysMsg(SM_QUIT));
		Common::String resp = _io.readLine();
		if (!resp.empty() && (resp[0] == 'Y' || resp[0] == 'y'))
			return ActionResult::Done;
		return ActionResult::Continue;
	}

	case AOP_END: {
		_io.print(sysMsg(SM_END));
		Common::String resp = _io.readLine();
		if (!resp.empty() && (resp[0] == 'Y' || resp[0] == 'y'))
			return ActionResult::InitialiseGame;
		_io.print(sysMsg(SM_GOODBYE));
		return ActionResult::SystemReset;
	}

	case AOP_DONE:
		return ActionResult::Done;

	case AOP_OK:
		_io.print(sysMsg(SM_OK));
		_io.newLine();
		return ActionResult::Done;

	case AOP_ANYKEY:
		_io.print(sysMsg(SM_ANY_KEY));
		_io.waitKey();
		return ActionResult::Continue;

	case AOP_SAVE:
		_io.doSaveGame();
		return ActionResult::MainGameLoop;

	case AOP_LOAD:
		_io.doLoadGame();
		return ActionResult::MainGameLoop;

	case AOP_TURNS: {
		uint16 turns = (turnsMSB() << 8) | turnsLSB();
		_io.print(Common::String::format("%d ", turns));
		_io.print(sysMsg(SM_TURN));
		if (turns != 1)
			_io.print(sysMsg(SM_S));
		_io.print(sysMsg(SM_FULL_STOP));
		_io.newLine();
		return ActionResult::Continue;
	}

	case AOP_SCORE:
		_io.print(sysMsg(SM_SCORED));
		_io.print(Common::String::format(" %d", score()));
		_io.print(sysMsg(SM_PERCENT));
		_io.newLine();
		return ActionResult::Continue;

	case AOP_CLS:
		_io.clearScreen();
		return ActionResult::Continue;

	case AOP_DROPALL:
		for (uint obj = 0; obj < _db.header().objectCount; ++obj) {
			byte loc = objectLocation(obj);
			if (loc == QLOC_CARRIED || loc == QLOC_WORN)
				setObjectLocation(obj, currentLocation());
		}
		objectsCarriedCount() = 0;
		return ActionResult::Continue;

	case AOP_PAUSE:
		return ActionResult::Continue; // Cosmetic timing only; nothing to simulate meaningfully here

	case AOP_SCREEN:
	case AOP_TEXT:
	case AOP_BORDER:
		// Atari GTIA colour changes have no equivalent in a windowed Glk text
		// view; accepted but currently a no-op. Only the *initial* colours
		// from the header are applied once, at startup (see initialiseGame).
		return ActionResult::Continue;

	case AOP_GOTO:
		currentLocation() = arg;
		return ActionResult::Continue;

	case AOP_MESSAGE:
		_io.print(_db.messageText(arg));
		_io.newLine();
		return ActionResult::Continue;

	case AOP_REMOVE:
		if (objectLocation(arg) != QLOC_WORN) {
			_io.print(sysMsg(SM_NOT_WEARING));
			_io.newLine();
			return ActionResult::Done;
		}
		setObjectLocation(arg, QLOC_CARRIED);
		return ActionResult::Continue;

	case AOP_GET:
		if (objectLocation(arg) == QLOC_CARRIED || objectLocation(arg) == QLOC_WORN) {
			_io.print(sysMsg(SM_ALREADY_HAVE));
			_io.newLine();
			return ActionResult::Done;
		}
		if (objectLocation(arg) != currentLocation()) {
			_io.print(sysMsg(SM_NOT_HERE));
			_io.newLine();
			return ActionResult::Done;
		}
		if (objectsCarriedCount() >= _db.header().maxCarry) {
			_io.print(sysMsg(SM_CANT_CARRY_MORE));
			_io.newLine();
			return ActionResult::Done;
		}
		setObjectLocation(arg, QLOC_CARRIED);
		objectsCarriedCount()++;
		_io.print(sysMsg(SM_OK));
		_io.newLine();
		return ActionResult::Continue;

	case AOP_DROP:
		if (objectLocation(arg) != QLOC_CARRIED && objectLocation(arg) != QLOC_WORN) {
			_io.print(sysMsg(SM_DONT_HAVE));
			_io.newLine();
			return ActionResult::Done;
		}
		if (objectLocation(arg) == QLOC_CARRIED && objectsCarriedCount() > 0)
			objectsCarriedCount()--;
		setObjectLocation(arg, currentLocation());
		_io.print(sysMsg(SM_OK));
		_io.newLine();
		return ActionResult::Continue;

	case AOP_WEAR:
		if (objectLocation(arg) == QLOC_WORN) {
			_io.print(sysMsg(SM_ALREADY_WORN));
			_io.newLine();
			return ActionResult::Done;
		}
		if (objectLocation(arg) != QLOC_CARRIED) {
			_io.print(sysMsg(SM_DONT_HAVE));
			_io.newLine();
			return ActionResult::Done;
		}
		if (objectsCarriedCount() > 0)
			objectsCarriedCount()--;
		setObjectLocation(arg, QLOC_WORN);
		_io.print(sysMsg(SM_OK));
		_io.newLine();
		return ActionResult::Continue;

	case AOP_DESTROY:
		setObjectLocation(arg, QLOC_NOTCREATED);
		return ActionResult::Continue;

	case AOP_CREATE:
		setObjectLocation(arg, currentLocation());
		return ActionResult::Continue;

	case AOP_SWAP: {
		byte tmp = objectLocation(arg);
		setObjectLocation(arg, objectLocation(a1));
		setObjectLocation(a1, tmp);
		return ActionResult::Continue;
	}

	case AOP_PLACE:
		setObjectLocation(arg, a1);
		return ActionResult::Continue;

	case AOP_SET:
		_flags[arg] = 0xFF;
		return ActionResult::Continue;

	case AOP_CLEAR:
		_flags[arg] = 0;
		return ActionResult::Continue;

	case AOP_PLUS:
		_flags[arg] = byte(MIN(255, int(_flags[arg]) + int(a1)));
		return ActionResult::Continue;

	case AOP_MINUS:
		_flags[arg] = byte(MAX(0, int(_flags[arg]) - int(a1)));
		return ActionResult::Continue;

	case AOP_LET:
		_flags[arg] = a1;
		return ActionResult::Continue;

	case AOP_SOUND:
		_io.sound(arg, a1, a2, a3);
		return ActionResult::Continue;

	default:
		return ActionResult::Continue;
	}
}

} // End of namespace Quill
} // End of namespace Glk
