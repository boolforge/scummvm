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

#include "glk/quill/database.h"
#include "common/md5.h"
#include "common/memstream.h"

namespace Glk {
namespace Quill {

// MD5 of the 3599-byte AdventureWriter/Quill Atari interpreter (offsets
// $7C0D-$8A1B), independently reproduced by reassembling the CA65
// disassembly published at https://locus-anatis.neocities.org/quill/atari/ .
// This identifies the interpreter itself, never a specific game's database,
// so it works as a detection anchor for *any* database paired with it.
const char *const QuillDatabase::INTERPRETER_MD5 = "b9107db7d9e6d4c4a884e8f7bc47a886";

const Common::Array<QuillConnection> QuillDatabase::_emptyConnections;

bool QuillDatabase::readXexSegments(Common::SeekableReadStream *stream, Common::Array<XexSegment> &segments) {
	stream->seek(0);
	bool first = true;

	while (!stream->eos() && stream->pos() < stream->size()) {
		if (stream->size() - stream->pos() < 4)
			break;

		uint16 word = stream->readUint16LE();
		uint16 start;

		if (word == 0xFFFF) {
			// Explicit segment marker - the following word is the real start address.
			if (stream->size() - stream->pos() < 4)
				break;
			start = stream->readUint16LE();
		} else if (first) {
			// The very first segment must begin with $FFFF - if it doesn't,
			// this isn't a DOS executable at all.
			return !segments.empty();
		} else {
			// Subsequent segments may omit the $FFFF marker; the word just
			// read is already the start address.
			start = word;
		}

		uint16 end = stream->readUint16LE();
		if (end < start)
			return !segments.empty();

		XexSegment seg;
		seg.start = start;
		seg.end = end;
		uint32 len = uint32(end) - uint32(start) + 1;
		seg.data.resize(len);
		uint32 got = stream->read(seg.data.data(), len);
		if (got != len)
			return !segments.empty();

		segments.push_back(seg);
		first = false;
	}

	return !segments.empty();
}

bool QuillDatabase::quickDetect(Common::SeekableReadStream *stream) {
	Common::Array<XexSegment> segments;
	if (!readXexSegments(stream, segments))
		return false;

	bool haveDatabase = false, haveInterpreter = false;

	for (uint i = 0; i < segments.size(); ++i) {
		const XexSegment &seg = segments[i];

		if (seg.start == DATABASE_BASE && seg.data.size() >= 31)
			haveDatabase = true;

		if (seg.start == INTERPRETER_BASE && seg.data.size() == INTERPRETER_SIZE) {
			Common::MemoryReadStream ms(seg.data.data(), seg.data.size());
			Common::String md5 = Common::computeStreamMD5AsString(ms);
			if (md5.equalsIgnoreCase(INTERPRETER_MD5))
				haveInterpreter = true;
		}
	}

	return haveDatabase && haveInterpreter;
}

bool QuillDatabase::load(Common::SeekableReadStream *stream, Common::String *error) {
	Common::Array<XexSegment> segments;
	if (!readXexSegments(stream, segments)) {
		if (error) *error = "Not a valid Atari DOS executable (.xex)";
		return false;
	}

	const XexSegment *dbSeg = nullptr;
	bool haveInterpreter = false;

	for (uint i = 0; i < segments.size(); ++i) {
		const XexSegment &seg = segments[i];
		if (seg.start == DATABASE_BASE)
			dbSeg = &segments[i];

		if (seg.start == INTERPRETER_BASE && seg.data.size() == INTERPRETER_SIZE) {
			Common::MemoryReadStream ms(seg.data.data(), seg.data.size());
			if (Common::computeStreamMD5AsString(ms).equalsIgnoreCase(INTERPRETER_MD5))
				haveInterpreter = true;
		}
	}

	if (!dbSeg) {
		if (error) *error = "No game database segment found at $1D00";
		return false;
	}
	if (!haveInterpreter) {
		if (error) *error = "Paired interpreter segment doesn't match the known AdventureWriter/Quill Atari interpreter - this may be a different program, or a Quill variant this engine doesn't support yet";
		return false;
	}
	if (dbSeg->data.size() < 31) {
		if (error) *error = "Database segment too small to contain a valid header";
		return false;
	}

	_data = dbSeg->data;
	parseHeader();

	if (_header.endOfDatabase != 0 && toOffset(_header.endOfDatabase) > _data.size()) {
		if (error) *error = "Database header end-of-database pointer is out of range";
		return false;
	}

	decodeTextTable(_header.objectTableAddr, _header.objectCount, _objectTexts);
	decodeTextTable(_header.locationTableAddr, _header.locationCount, _locationTexts);
	decodeTextTable(_header.messageTableAddr, _header.messageCount, _messageTexts);
	decodeTextTable(_header.systemMessageTableAddr, _header.systemMessageCount, _systemMessageTexts);
	decodeVocabulary(_header.vocabularyTableAddr);
	decodeEventTable(_header.eventTableAddr, _events);
	decodeEventTable(_header.statusTableAddr, _statuses);
	decodeConnections();

	// Object start locations: a flat byte array, one per object, terminated by QTAB_END.
	_objectStartLocations.clear();
	uint16 off = toOffset(_header.objectLocationTableAddr);
	for (uint i = 0; i < _header.objectCount && off < _data.size(); ++i, ++off) {
		byte b = _data[off];
		_objectStartLocations.push_back(b);
	}

	return true;
}

void QuillDatabase::parseHeader() {
	// The header is 31 bytes, matching the Atari memory layout documented at
	// https://locus-anatis.neocities.org/quill/atari/file-format.html
	uint16 p = 0;
	auto rd8 = [&]() -> byte { return (p < _data.size()) ? _data[p++] : 0; };
	auto rd16 = [&]() -> uint16 {
		uint16 lo = rd8();
		uint16 hi = rd8();
		return lo | (hi << 8);
	};

	_header.unused0 = rd8();
	_header.color1 = rd8();
	_header.color2 = rd8();
	_header.color4 = rd8();
	_header.maxCarry = rd8();
	_header.objectCount = rd8();
	_header.locationCount = rd8();
	_header.messageCount = rd8();
	_header.systemMessageCount = rd8();
	_header.eventTableAddr = rd16();
	_header.statusTableAddr = rd16();
	_header.objectTableAddr = rd16();
	_header.locationTableAddr = rd16();
	_header.messageTableAddr = rd16();
	_header.systemMessageTableAddr = rd16();
	_header.movementTableAddr = rd16();
	_header.vocabularyTableAddr = rd16();
	_header.objectLocationTableAddr = rd16();
	_header.endOfDatabase = rd16();
	_header.unused29 = rd16();
}

Common::String QuillDatabase::decodeString(uint16 offset, uint16 *outEndOffset) const {
	Common::String result;
	bool inverse = false;

	while (offset < _data.size()) {
		byte raw = _data[offset++];
		byte decoded = raw ^ 0xFF;

		if (decoded == 0)
			break; // String terminator

		if (decoded == 0x9B) { // NEWLINE
			if (inverse) { result += INVERSE_OFF; inverse = false; }
			result += '\n';
			continue;
		}

		bool wantInverse = (decoded & 0x80) != 0;
		char ch = char(decoded & 0x7F);

		if (wantInverse != inverse) {
			result += wantInverse ? INVERSE_ON : INVERSE_OFF;
			inverse = wantInverse;
		}
		result += ch;
	}

	if (inverse)
		result += INVERSE_OFF;

	if (outEndOffset)
		*outEndOffset = offset;
	return result;
}

void QuillDatabase::decodeTextTable(uint16 tableAddr, uint16 count, Common::Array<Common::String> &out) {
	out.clear();
	uint16 tableOff = toOffset(tableAddr);

	for (uint16 i = 0; i < count; ++i) {
		uint16 entryOff = tableOff + i * 2;
		if (entryOff + 1 >= _data.size()) {
			out.push_back(Common::String());
			continue;
		}
		uint16 textAddr = _data[entryOff] | (_data[entryOff + 1] << 8);
		out.push_back(decodeString(toOffset(textAddr)));
	}
}

void QuillDatabase::decodeVocabulary(uint16 tableAddr) {
	_vocabulary.clear();
	uint16 off = toOffset(tableAddr);

	while (off + 5 <= _data.size()) {
		byte raw[4] = { _data[off], _data[off + 1], _data[off + 2], _data[off + 3] };
		bool terminator = false;
		for (int i = 0; i < 4; ++i) {
			if (raw[i] == 0x00) { // decodes to $FF - table/scan terminator
				terminator = true;
				break;
			}
		}
		if (terminator)
			break;

		QuillWord w;
		for (int i = 0; i < 4; ++i) {
			byte decoded = raw[i] ^ 0xFF;
			if (decoded == byte(' ' ^ 0)) {
				// Nothing special - spaces are valid padding, kept as-is below.
			}
			w.text += char(decoded);
		}
		while (!w.text.empty() && w.text.lastChar() == ' ')
			w.text.deleteLastChar();

		w.number = _data[off + 4];
		_vocabulary.push_back(w);
		off += 5;
	}
}

void QuillDatabase::decodeEventTable(uint16 tableAddr, Common::Array<QuillEventEntry> &out) {
	out.clear();
	uint16 off = toOffset(tableAddr);

	while (off + 4 <= _data.size()) {
		byte word1 = _data[off];
		if (word1 == 0)
			break; // Word1 == 0 terminates the event/status table

		QuillEventEntry e;
		e.word1 = word1;
		e.word2 = _data[off + 1];
		uint16 condactAddr = _data[off + 2] | (_data[off + 3] << 8);
		e.condactAddr = toOffset(condactAddr);
		out.push_back(e);
		off += 4;
	}
}

void QuillDatabase::decodeConnections() {
	_connections.clear();
	_connections.resize(_header.locationCount);

	uint16 ptrTableOff = toOffset(_header.movementTableAddr);

	for (uint16 loc = 0; loc < _header.locationCount; ++loc) {
		uint16 entryOff = ptrTableOff + loc * 2;
		if (entryOff + 1 >= _data.size())
			continue;

		uint16 listAddr = _data[entryOff] | (_data[entryOff + 1] << 8);
		uint16 off = toOffset(listAddr);

		while (off + 1 < _data.size()) {
			byte word = _data[off];
			if (word == QTAB_END)
				break;
			QuillConnection c;
			c.word = word;
			c.targetLocation = _data[off + 1];
			_connections[loc].push_back(c);
			off += 2;
		}
	}
}

const Common::Array<QuillConnection> &QuillDatabase::connectionsForLocation(uint location) const {
	if (location < _connections.size())
		return _connections[location];
	return _emptyConnections;
}

Common::String QuillDatabase::objectText(uint index) const {
	return (index < _objectTexts.size()) ? _objectTexts[index] : Common::String();
}

Common::String QuillDatabase::locationText(uint index) const {
	return (index < _locationTexts.size()) ? _locationTexts[index] : Common::String();
}

Common::String QuillDatabase::messageText(uint index) const {
	return (index < _messageTexts.size()) ? _messageTexts[index] : Common::String();
}

Common::String QuillDatabase::systemMessageText(uint index) const {
	return (index < _systemMessageTexts.size()) ? _systemMessageTexts[index] : Common::String();
}

} // End of namespace Quill
} // End of namespace Glk
