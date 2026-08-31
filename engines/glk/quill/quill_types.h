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

#ifndef GLK_QUILL_TYPES_H
#define GLK_QUILL_TYPES_H

#include "common/scummsys.h"
#include "common/str.h"
#include "common/array.h"

/**
 * Types, opcodes and constants for the Atari 800/XL/XE version of
 * The Quill / AdventureWriter interpreter.
 *
 * Derived from a CA65 disassembly of the original interpreter (by
 * EdwardianDuck, using the py8dis tool) and the accompanying file format
 * documentation at https://locus-anatis.neocities.org/quill/atari/ .
 *
 * The Atari version sits between Quill version A and version C: system
 * messages are data-driven (unlike version A) but it doesn't implement
 * AUTO opcodes or object words (unlike version C).
 */

namespace Glk {
namespace Quill {

// Special object-location / table sentinel byte values
enum SpecialByte {
	QLOC_NOTCREATED = 252,
	QLOC_WORN       = 253,
	QLOC_CARRIED    = 254,
	QTAB_END        = 255,  ///< Table terminator; also used as the "any word" wildcard
	QWORD_WILDCARD  = 255
};

// The 33 system messages, in their fixed database order. Text for these
// comes from the database (unlike the ZX Spectrum "Quill A" interpreter,
// where they're hard-coded), but the *number* of each is fixed by the
// interpreter and referenced directly from its code.
enum SystemMessageId {
	SM_DARK             = 0,
	SM_ALSO_SEE         = 1,
	SM_PROMPT1          = 2,
	SM_PROMPT2          = 3,
	SM_PROMPT3          = 4,
	SM_PROMPT4          = 5,
	SM_DONT_UNDERSTAND  = 6,
	SM_CANT_GO          = 7,
	SM_CANT_DO          = 8,
	SM_HAVE_WITH_ME     = 9,
	SM_WORN             = 10,
	SM_NOTHING          = 11,
	SM_QUIT             = 12,
	SM_END              = 13,
	SM_GOODBYE          = 14,
	SM_OK               = 15,
	SM_ANY_KEY          = 16,
	SM_YOU_HAVE_TAKEN   = 17,
	SM_TURN             = 18,
	SM_S                = 19,
	SM_FULL_STOP        = 20,
	SM_SCORED           = 21,
	SM_PERCENT          = 22,
	SM_NOT_WEARING      = 23,
	SM_HANDS_FULL       = 24,
	SM_ALREADY_HAVE     = 25,
	SM_NOT_HERE         = 26,
	SM_CANT_CARRY_MORE  = 27,
	SM_DONT_HAVE        = 28,
	SM_ALREADY_WORN     = 29,
	SM_Y                = 30,
	SM_N                = 31,
	SM_FILENAME         = 32,
	SM_COUNT            = 33
};

// A word matching < 13 (FIRSTNONDIRECTIONWORD) is treated as a direction
// for the purposes of picking between "You can't go..." and "You can't do..."
const byte FIRST_NON_DIRECTION_WORD = 13;

/**
 * Condition opcodes ("COP_*" in the original disassembly). Each one always
 * consumes exactly one argument byte immediately after the opcode byte,
 * used generically by the dispatcher as an object number, flag number,
 * location number or literal value depending on the specific opcode -
 * EQ/GT/LT additionally consume one more byte (a comparison constant).
 */
enum ConditionOpcode {
	COP_AT       = 0x00, ///< arg: location.  Player at location == arg
	COP_NOTAT    = 0x01, ///< arg: location.  Player at location != arg
	COP_ATGT     = 0x02, ///< arg: location.  Player at location > arg
	COP_ATLT     = 0x03, ///< arg: location.  Player at location < arg
	COP_PRESENT  = 0x04, ///< arg: object.    Object carried, worn or here
	COP_ABSENT   = 0x05, ///< arg: object.    Object neither carried, worn nor here
	COP_WORN     = 0x06, ///< arg: object.    Object is worn
	COP_NOTWORN  = 0x07, ///< arg: object.    Object is not worn
	COP_CARRIED  = 0x08, ///< arg: object.    Object is carried (not worn)
	COP_NOTCARR  = 0x09, ///< arg: object.    Object is not carried
	COP_CHANCE   = 0x0A, ///< arg: percentage. True if arg > random(1..100)
	COP_ZERO     = 0x0B, ///< arg: flag.      Flag == 0
	COP_NOTZERO  = 0x0C, ///< arg: flag.      Flag != 0
	COP_EQ       = 0x0D, ///< arg: flag, constant. Flag == constant
	COP_GT       = 0x0E, ///< arg: flag, constant. Flag > constant
	COP_LT       = 0x0F, ///< arg: flag, constant. Flag < constant
	COP_COUNT    = 16
};

/**
 * Action opcodes ("AOP_*" in the original disassembly). As with condition
 * opcodes, every action opcode consumes exactly one dispatch byte right
 * after the opcode (interpreted as object/flag/location/literal depending
 * on the opcode, and simply ignored by opcodes that don't need it); some
 * consume additional bytes on top of that.
 */
enum ActionOpcode {
	AOP_INV      = 0x00, ///< 0 args (+ dispatch byte). List inventory. Always ends condact processing.
	AOP_DESC     = 0x01, ///< 0 args. Describe current location (restarts the main loop)
	AOP_QUIT     = 0x02, ///< 0 args. Prompt player to quit
	AOP_END      = 0x03, ///< 0 args. End game, prompt to try again / reset
	AOP_DONE     = 0x04, ///< 0 args. Stop processing actions for this condact
	AOP_OK       = 0x05, ///< 0 args. Print "OK." and stop processing (like DONE)
	AOP_ANYKEY   = 0x06, ///< 0 args. "Press any key..." and wait
	AOP_SAVE     = 0x07, ///< 0 args. Save game (restarts the main loop after)
	AOP_LOAD     = 0x08, ///< 0 args. Load game (restarts the main loop after)
	AOP_TURNS    = 0x09, ///< 0 args. Print number of turns taken
	AOP_SCORE    = 0x0A, ///< 0 args. Print score out of 100
	AOP_CLS      = 0x0B, ///< 0 args. Clear screen
	AOP_DROPALL  = 0x0C, ///< 0 args. Drop all carried/worn objects here
	AOP_PAUSE    = 0x0D, ///< 1 arg: ticks (1/50s). Pause
	AOP_SCREEN   = 0x0E, ///< 1 arg: colour value. Set background colour
	AOP_TEXT     = 0x0F, ///< 1 arg: luminosity 0..127. Set text luminosity (doubled)
	AOP_BORDER   = 0x10, ///< 1 arg: colour value. Set border colour
	AOP_GOTO     = 0x11, ///< 1 arg: location. Move player
	AOP_MESSAGE  = 0x12, ///< 1 arg: message number. Print a database message
	AOP_REMOVE   = 0x13, ///< 1 arg: object. Take off a worn object (-> carried)
	AOP_GET      = 0x14, ///< 1 arg: object. Pick up an object
	AOP_DROP     = 0x15, ///< 1 arg: object. Drop a carried/worn object
	AOP_WEAR     = 0x16, ///< 1 arg: object. Wear a carried object
	AOP_DESTROY  = 0x17, ///< 1 arg: object. Remove object from play (NOTCREATED)
	AOP_CREATE   = 0x18, ///< 1 arg: object. Bring object into current location
	AOP_SWAP     = 0x19, ///< 2 args: object1, object2. Swap their locations
	AOP_PLACE    = 0x1A, ///< 2 args: object, location. Move object to location
	AOP_SET      = 0x1B, ///< 1 arg: flag. Flag = 0xFF
	AOP_CLEAR    = 0x1C, ///< 1 arg: flag. Flag = 0
	AOP_PLUS     = 0x1D, ///< 2 args: flag, constant. Flag += constant, clamp 255
	AOP_MINUS    = 0x1E, ///< 2 args: flag, constant. Flag -= constant, clamp 0
	AOP_LET      = 0x1F, ///< 2 args: flag, constant. Flag = constant
	AOP_SOUND    = 0x20, ///< 4 args: voice, pitch, distortion, volume
	AOP_COUNT    = 33
};

/**
 * What an action opcode handler wants the VM to do once it has finished.
 * The original interpreter is written in goto-heavy 6502 assembly with
 * several genuinely different exit routes out of action-opcode processing
 * (not just "next opcode" / "stop"), so this enum mirrors those exactly
 * rather than forcing everything through a uniform loop.
 */
enum class ActionResult {
	Continue,        ///< Advance to the next action opcode in this condact (ProceedToNextOpcode)
	Done,            ///< Stop processing this condact's actions; resume outer event/status scan (AOP_DONE)
	MainGameLoop,    ///< Abandon everything, redescribe location & do a full turn (AOP_DESC/SAVE/LOAD)
	InitialiseGame,  ///< Restart the whole game (AOP_END, "try again" response)
	SystemReset      ///< Player confirmed quitting for good (AOP_END, "no" to try again)
};

/**
 * The 31-byte Quill Atari database header, found at the start of the
 * game database (offset 0 in our loaded buffer, which corresponds to
 * Atari address $1D00).
 */
struct QuillHeader {
	byte unused0;
	byte color1;               ///< Text luminance, already multiplied by 2 (COLOR1)
	byte color2;                ///< Background colour (COLOR2)
	byte color4;                ///< Border colour (COLOR4)
	byte maxCarry;              ///< Max number of objects that can be carried
	byte objectCount;
	byte locationCount;
	byte messageCount;
	byte systemMessageCount;
	uint16 eventTableAddr;
	uint16 statusTableAddr;
	uint16 objectTableAddr;
	uint16 locationTableAddr;
	uint16 messageTableAddr;
	uint16 systemMessageTableAddr;
	uint16 movementTableAddr;   ///< AKA "connections" table in the file format docs
	uint16 vocabularyTableAddr;
	uint16 objectLocationTableAddr;
	uint16 endOfDatabase;
	uint16 unused29;
};

/// A single decoded vocabulary word: up to 4 characters plus its word number.
struct QuillWord {
	Common::String text;   ///< Decoded, trimmed (may be shorter than 4 chars)
	byte number = 0;
};

/// One entry in an event or status table (4 bytes each in the database).
struct QuillEventEntry {
	byte word1 = 0;
	byte word2 = 0;
	uint16 condactAddr = 0;   ///< Offset into our loaded buffer (NOT an Atari address)
};

/// One word/target-location pair from a location's movement/connections list.
struct QuillConnection {
	byte word = 0;
	byte targetLocation = 0;
};

} // End of namespace Quill
} // End of namespace Glk

#endif
