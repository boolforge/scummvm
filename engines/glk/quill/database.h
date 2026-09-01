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

#ifndef GLK_QUILL_DATABASE_H
#define GLK_QUILL_DATABASE_H

#include "common/stream.h"
#include "common/array.h"
#include "common/str.h"
#include "glk/quill/quill_types.h"

namespace Glk {
namespace Quill {

/**
 * Loads and decodes a Quill/AdventureWriter Atari game database from a raw
 * Atari DOS "segmented" executable (.XEX).
 *
 * An .XEX is a sequence of segments, each `$FFFF start end <data>` (the
 * leading $FFFF marker is only mandatory before the very first segment;
 * later segments may omit it). A genuine AdventureWriter Atari release
 * ships 3 segments: the game database at $1D00, the interpreter itself at
 * $7C0D-$8A1B, and a 2-byte DOS autostart block at $02E0 pointing at the
 * interpreter's entry point. We only need the database segment to run the
 * game - the interpreter segment is used purely to *validate* that this is
 * really a compatible AdventureWriter Atari release (by checksum), since
 * we provide our own reimplementation of it.
 *
 * Decoded text uses two inline control bytes as lightweight markup for
 * inverse-video runs (the database's only text decoration): 0x01 begins an
 * inverse-video run, 0x02 ends one. These are stripped/interpreted by the
 * engine's print routine and never form part of visible output.
 */
class QuillDatabase {
public:
	static const uint16 DATABASE_BASE = 0x1D00;   ///< Atari address the database loads at
	static const uint16 INTERPRETER_BASE = 0x7C0D; ///< Atari address the interpreter loads at
	static const uint32 INTERPRETER_SIZE = 3599;   ///< Exact size of the interpreter code+data
	static const char *const INTERPRETER_MD5;      ///< MD5 of the known-good interpreter bytes

	static const char INVERSE_ON = 0x01;
	static const char INVERSE_OFF = 0x02;

	/**
	 * Attempts to load a full game (database + validating the paired
	 * interpreter segment) from an .XEX stream. Returns false and sets
	 * *error on failure (stream ownership remains with the caller).
	 */
	bool load(Common::SeekableReadStream *stream, Common::String *error = nullptr);

	/**
	 * Lightweight structural check for detection purposes: does this
	 * stream look like a DOS-segmented Atari executable containing our
	 * known-good AdventureWriter interpreter, paired with a plausible
	 * database segment at $1D00? Doesn't decode any tables. If matched,
	 * optionally returns the whole-file MD5-relevant size hints.
	 */
	static bool quickDetect(Common::SeekableReadStream *stream);

	const QuillHeader &header() const { return _header; }

	Common::String objectText(uint index) const;
	Common::String locationText(uint index) const;
	Common::String messageText(uint index) const;
	Common::String systemMessageText(uint index) const;

	const Common::Array<QuillWord> &vocabulary() const { return _vocabulary; }
	const Common::Array<QuillEventEntry> &events() const { return _events; }
	const Common::Array<QuillEventEntry> &statuses() const { return _statuses; }

	/// Raw start-location byte per object, terminated by QTAB_END; index 0..objectCount-1 valid.
	const Common::Array<byte> &objectStartLocations() const { return _objectStartLocations; }

	/// Movement/connections list for one location (0-based), empty if out of range.
	const Common::Array<QuillConnection> &connectionsForLocation(uint location) const;

	/// Direct byte access into the loaded database, offset from $1D00 - used by the VM
	/// to walk condact streams exactly as the original interpreter does.
	byte byteAt(uint16 offset) const {
		return (offset < _data.size()) ? _data[offset] : QTAB_END;
	}

	uint16 toOffset(uint16 atariAddr) const { return atariAddr - DATABASE_BASE; }

private:
	struct XexSegment {
		uint16 start = 0, end = 0;
		Common::Array<byte> data;
	};

	QuillHeader _header;
	Common::Array<byte> _data;             ///< Index 0 == Atari address $1D00
	Common::Array<Common::String> _objectTexts;
	Common::Array<Common::String> _locationTexts;
	Common::Array<Common::String> _messageTexts;
	Common::Array<Common::String> _systemMessageTexts;
	Common::Array<QuillWord> _vocabulary;
	Common::Array<QuillEventEntry> _events;
	Common::Array<QuillEventEntry> _statuses;
	Common::Array<byte> _objectStartLocations;
	Common::Array<Common::Array<QuillConnection>> _connections;
	static const Common::Array<QuillConnection> _emptyConnections;

	static bool readXexSegments(Common::SeekableReadStream *stream, Common::Array<XexSegment> &segments);

	void parseHeader();
	Common::String decodeString(uint16 offset, uint16 *outEndOffset = nullptr) const;
	void decodeTextTable(uint16 tableAddr, uint16 count, Common::Array<Common::String> &out);
	void decodeVocabulary(uint16 tableAddr);
	void decodeEventTable(uint16 tableAddr, Common::Array<QuillEventEntry> &out);
	void decodeConnections();
};

} // End of namespace Quill
} // End of namespace Glk

#endif
