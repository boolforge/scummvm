/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "paco1994/ald_parser.h"

#include "common/debug.h"
#include "common/str.h"
#include "common/textconsole.h"

namespace Paco1994 {

// ── _codifica ─────────────────────────────────────────────────────────────────
void AldParser::_codifica(Common::Array<byte> &data) {
	// Exact replica of HARE.EXE TDS symbol _codifica(char *buf, int len):
	//   for (int i = 0; i < len; i++) buf[i] ^= 0xFF;
	// XOR with 0xFF is identical to bitwise NOT on an unsigned byte.
	// Applied to decrypt: encrypted_byte XOR 0xFF = original_byte.
	// Applied to encrypt: original_byte XOR 0xFF = encrypted_byte.
	// (The same operation is its own inverse — calling twice returns original.)
	for (uint i = 0; i < data.size(); i++)
		data[i] ^= 0xFF;
}

// ── splitLines ────────────────────────────────────────────────────────────────
Common::Array<Common::String> AldParser::splitLines(const Common::Array<byte> &data) {
	// QuickBASIC PRINT #1 writes each value followed by CRLF (0x0D 0x0A).
	// After _codifica() the data is plain ASCII text with \r\n line endings.
	// We split on \n (ignoring \r) and strip leading/trailing whitespace.
	// Empty lines are discarded (QB may write extra blank lines).
	//
	// NUMERIC QUIRK: QB PRINT #1, integer_value produces " NNN " with a
	// leading space and trailing space. parseInt() handles this via strip().
	Common::Array<Common::String> lines;
	Common::String cur;

	for (uint i = 0; i < data.size(); i++) {
		byte b = data[i];
		if (b == '\r') continue;  // skip CR, only act on LF
		if (b == '\n') {
			cur.trim();
			if (!cur.empty())
				lines.push_back(cur);
			cur.clear();
		} else {
			cur += (char)b;
		}
	}
	// Flush last line if file doesn't end with \n
	cur.trim();
	if (!cur.empty())
		lines.push_back(cur);

	return lines;
}

// ── parseInt ──────────────────────────────────────────────────────────────────
int16 AldParser::parseInt(const Common::String &line) {
	// QB writes " 20 \r\n" for integer 20. After trim: "20".
	Common::String s(line);
	s.trim();
	return (int16)atoi(s.c_str());
}

// ── sceneIdFromFilename ───────────────────────────────────────────────────────
Common::String AldParser::sceneIdFromFilename(const Common::String &filename) {
	// "10.ALD" → "10", "1.ALD" → "1"
	Common::String id(filename);
	// Find the dot and truncate
	for (uint i = 0; i < id.size(); i++) {
		if (id[i] == '.') {
			id = id.substr(0, i);
			break;
		}
	}
	// Uppercase comparison: ALD files may be delivered as "1.ald" on case-sensitive FS
	id.toUppercase();
	return id;
}

// ── parseScene ────────────────────────────────────────────────────────────────
bool AldParser::parseScene(Common::SeekableReadStream &stream, AldScene &scene) {
	// Step 1: Read entire raw file into buffer
	uint32 fileSize = (uint32)stream.size();
	Common::Array<byte> rawData;
	rawData.resize(fileSize);
	if (stream.read(rawData.data(), fileSize) != fileSize) {
		warning("AldParser: Could not read .ALD stream (expected %u bytes)", fileSize);
		return false;
	}

	// Step 2: Apply _codifica() — XOR each byte with 0xFF
	_codifica(rawData);

	// Step 3: Split into clean lines
	Common::Array<Common::String> lines = splitLines(rawData);
	if (lines.size() < 4) {
		warning("AldParser: .ALD too short (got %u lines, need >=4)", lines.size());
		return false;
	}

	// Step 4: Parse header fields
	uint cursor = 0;
	scene.sceneId  = lines[cursor++];  // Line 1: P$ — scene identifier
	scene.musicFile = lines[cursor++]; // Line 2: MU$(0) — CMF filename
	int16 nop = parseInt(lines[cursor++]); // Line 3: NOP(0) — object count
	int16 nod = parseInt(lines[cursor++]); // Line 4: NOD(0) — disc-object count

	debugC(1, kDebugALD, "AldParser: scene='%s' music='%s' NOP=%d NOD=%d",
	       scene.sceneId.c_str(), scene.musicFile.c_str(), nop, nod);

	// Step 5: Parse NOP on-screen objects (hotspots and doors)
	// This is the FOR I=1 TO NOP(0) loop in FUNCTION GRABAR.
	scene.objects.clear();
	for (int16 i = 0; i < nop; i++) {
		if (cursor + 5 >= (uint)lines.size()) {
			warning("AldParser: Truncated object list at object %d", i);
			return false;
		}

		AldObject obj;
		obj.id   = parseInt(lines[cursor++]);  // NUM(I)
		int16 x1 = parseInt(lines[cursor++]);  // X1(I)
		int16 y1 = parseInt(lines[cursor++]);  // Y1(I)
		int16 x2 = parseInt(lines[cursor++]);  // x2(I)
		int16 y2 = parseInt(lines[cursor++]);  // y2(I)
		obj.rect = Common::Rect(x1, y1, x2 + 1, y2 + 1); // ScummVM rects are half-open

		int16 isDoor = parseInt(lines[cursor++]); // PUER(I): 0=object, 1=door
		obj.isDoor = (isDoor != 0);

		if (obj.isDoor) {
			// Door-only fields: 3 more lines
			if (cursor + 2 >= (uint)lines.size()) {
				warning("AldParser: Truncated door fields for object %d", obj.id);
				return false;
			}
			obj.destScene  = lines[cursor++];           // e.g. "5.ALD"
			obj.destDoorId = parseInt(lines[cursor++]); // PDN(I)
			obj.destPos    = parseInt(lines[cursor++]); // PPOS(I): 0=left,1=right,2=center
		}

		debugC(2, kDebugALD,
		       "  Object id=%d rect=(%d,%d,%d,%d) isDoor=%d%s",
		       obj.id, x1, y1, x2, y2, (int)obj.isDoor,
		       obj.isDoor ? Common::String::format(
		           " dest='%s' doorId=%d pos=%d",
		           obj.destScene.c_str(), obj.destDoorId, obj.destPos).c_str() : "");

		scene.objects.push_back(obj);
	}

	// Step 6: Validate terminator "99"
	if (cursor >= (uint)lines.size() || lines[cursor] != "99") {
		warning("AldParser: Expected terminator '99' at line %u, got '%s'",
		        cursor, cursor < (uint)lines.size() ? lines[cursor].c_str() : "<EOF>");
		return false;
	}
	cursor++; // consume "99"

	// Step 7: Parse NOD disc-object sprites
	// These are sprites from 99.ALG placed at specific positions in the scene.
	// IMPORTANT: sheetRect.bottom = sheetRect.top + 39 (BUGGY in GRABAR).
	// Actual sprite height = 24px (TAMANIOY = 24 in SCOVA.ALC, from QB GET bounds).
	// When blitting, use height = AldDiscObject::kTrueHeight = 24, NOT sheetRect.height().
	scene.discObjects.clear();
	for (int16 i = 0; i < nod; i++) {
		if (cursor + 6 >= (uint)lines.size()) {
			warning("AldParser: Truncated disc-object list at object %d", i);
			return false;
		}

		AldDiscObject dobj;
		dobj.spriteId = parseInt(lines[cursor++]); // NUOD(I) — column in 99.ALG
		int16 sx1 = parseInt(lines[cursor++]);      // POX(I)
		int16 sy1 = parseInt(lines[cursor++]);      // POY(I)
		int16 sx2 = parseInt(lines[cursor++]);      // POX(I) + 39  (= sx1 + TAMANIOX)
		int16 sy2 = parseInt(lines[cursor++]);      // POY(I) + 39  (BUG: should be +24)
		int16 px  = parseInt(lines[cursor++]);      // PX(I)
		int16 py  = parseInt(lines[cursor++]);      // PY(I)

		// Store buggy sheetRect as-is for fidelity; callers must use kTrueHeight.
		dobj.sheetRect = Common::Rect(sx1, sy1, sx2 + 1, sy2 + 1);
		dobj.place     = Common::Point(px, py);

		debugC(2, kDebugALD,
		       "  DiscObj id=%d sheet=(%d,%d,%d,%d) [trueH=%d] place=(%d,%d)",
		       dobj.spriteId, sx1, sy1, sx2, sy2, AldDiscObject::kTrueHeight, px, py);

		scene.discObjects.push_back(dobj);
	}

	debugC(1, kDebugALD, "AldParser: scene '%s' parsed OK (%u objs, %u discObjs)",
	       scene.sceneId.c_str(), (uint)scene.objects.size(), (uint)scene.discObjects.size());

	return true;
}

} // namespace Paco1994
