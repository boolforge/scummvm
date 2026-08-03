/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * @file ald_parser.h
 * @brief Parser for Alcachofa Soft .ALD (Area Layout Data) scene files — 1994 format.
 *
 * FORMAT SPECIFICATION (100% empirically verified):
 *
 * .ALD files are PLAIN TEXT written by FUNCTION GRABAR in SCOVA.ALC (QuickBASIC 4.5
 * level editor source). The files are XOR-encrypted byte-by-byte with 0xFF before
 * distribution. The game's _codifica() function decodes them at load time.
 *
 * ENCRYPTION:
 *   each_byte_on_disk = decoded_byte XOR 0xFF   (bitwise NOT / one's complement)
 *   _codifica(buf, len): for(i=0;i<len;i++) buf[i]^=0xFF;   [HARE.EXE TDS confirmed]
 *
 * DELIMITER:
 *   QuickBASIC PRINT #1 writes CRLF (0x0D 0x0A).
 *   After XOR-0xFF: 0x0D→0xF2, 0x0A→0xF5. On disk: lines end 0xF2 0xF5.
 *
 * NUMERIC FORMAT:
 *   QB PRINT #1, integer → " NNN " (leading/trailing space + CRLF). Strip whitespace.
 *
 * STRUCTURE (after decode):
 *   Line 1  : P$      — Scene ID string ("1", "10", "11")
 *   Line 2  : MU$(0)  — CMF music filename ("AULD.CMF", "STARFM.CMF")
 *   Line 3  : NOP(0)  — Integer: count of on-screen objects (hotspots+doors)
 *   Line 4  : NOD(0)  — Integer: count of disc-object sprites (from 99.ALG)
 *   [NOP times]:
 *     NUM(I)   — Object ID (global, unique across scenes)
 *     X1(I)    — Hotspot rect left   (0-319)
 *     Y1(I)    — Hotspot rect top    (0-199)
 *     x2(I)    — Hotspot rect right  (0-319)
 *     y2(I)    — Hotspot rect bottom (0-199)
 *     PUER(I)  — 0=interactive object, 1=navigation door/exit
 *     [if PUER==1]:
 *       dest_scene  — "N.ALD" filename e.g. "5.ALD"
 *       PDN(I)      — Door ID in destination scene
 *       PPOS(I)     — Entry position: 0=left(x≈20), 1=right(x≈300), 2=center(x=160)
 *   "99"     — Literal terminator string
 *   [NOD times]:
 *     NUOD(I)         — Sprite column index in 99.ALG
 *     POX(I)          — Sprite X1 in 99.ALG pixel coords
 *     POY(I)          — Sprite Y1 in 99.ALG pixel coords
 *     POX(I)+39       — Sprite X2 (= X1 + TAMANIOX = X1 + 39)
 *     POY(I)+39       — Sprite Y2 — BUG: uses TAMANIOX (39) not TAMANIOY (24)
 *                       Actual sprite height in 99.ALG = 24px. Use height=24 when blitting.
 *     PX(I), PY(I)    — Scene render position (x,y)
 */

#ifndef PACO1994_ALD_PARSER_H
#define PACO1994_ALD_PARSER_H

#include "common/array.h"
#include "common/rect.h"
#include "common/str.h"
#include "common/stream.h"

namespace Paco1994 {

// ---------------------------------------------------------------------------
// AldObject — one entry from the NOP (on-screen object) section
// ---------------------------------------------------------------------------

/**
 * Represents a hotspot or door in a scene.
 * Parsed from the NOP section of a decoded .ALD file.
 */
struct AldObject {
	int16          id;          ///< NUM(I) — globally unique object ID
	Common::Rect   rect;        ///< (X1,Y1,X2,Y2) in 320×200 pixel coords
	bool           isDoor;      ///< PUER(I): true = navigation exit

	// Door-only fields (only valid when isDoor == true):
	Common::String destScene;   ///< e.g. "5.ALD" — destination .ALD filename
	int16          destDoorId;  ///< PDN(I) — matching door ID in destination scene
	int16          destPos;     ///< PPOS(I): 0=left, 1=right, 2=center

	AldObject() : id(0), isDoor(false), destDoorId(0), destPos(0) {}
};

// ---------------------------------------------------------------------------
// AldDiscObject — one entry from the NOD (disc-object sprite) section
// ---------------------------------------------------------------------------

/**
 * Represents a sprite from 99.ALG placed in the current scene.
 * Parsed from the NOD section of a decoded .ALD file.
 *
 * CRITICAL BUG NOTE (from SCOVA.ALC FUNCTION GRABAR source code):
 *   GRABAR writes: PRINT #1, POY(I) + TAMANIOX(0)  [for sheet.bottom]
 *   TAMANIOX(0) = 39, but TAMANIOY(0) = 24.
 *   → sheetRect.bottom = sheetRect.top + 39 (WRONG, should be +24).
 *   → When blitting from 99.ALG, always use height = 24, not sheetRect.height().
 *   → The mask row in 99.ALG is at sheetRect.top + 56 (Y offset from sprite row origin).
 */
struct AldDiscObject {
	int16        spriteId;    ///< NUOD(I) — column index in 99.ALG sprite sheet
	Common::Rect sheetRect;   ///< (POX,POY,POX+39,POY+39) — BUGGY Y2, see above
	Common::Point place;      ///< (PX,PY) — scene render position

	static constexpr int kTrueHeight = 24;   ///< Actual sprite height (TAMANIOY = 24)
	static constexpr int kWidth      = 39;   ///< Sprite width (TAMANIOX = 39)
	static constexpr int kMaskYOff   = 56;   ///< Mask row Y-offset from sprite row

	AldDiscObject() : spriteId(0) {}
};

// ---------------------------------------------------------------------------
// AldScene — complete parsed scene descriptor
// ---------------------------------------------------------------------------

struct AldScene {
	Common::String             sceneId;      ///< P$ — e.g. "1", "10", "11"
	Common::String             musicFile;    ///< MU$(0) — e.g. "AULD.CMF"
	Common::Array<AldObject>   objects;      ///< On-screen objects (NOP entries)
	Common::Array<AldDiscObject> discObjects; ///< Disc-object sprites (NOD entries)
};

// ---------------------------------------------------------------------------
// AldParser — stateless parser. Call parseScene() on a decoded ALD buffer.
// ---------------------------------------------------------------------------

/**
 * Parses a decoded (XOR-0xFF applied) .ALD scene data file.
 *
 * Usage:
 *   Common::File f;
 *   f.open("1.ALD");
 *   AldParser parser;
 *   AldScene scene;
 *   if (!parser.parseScene(f, scene))
 *       error("Failed to parse 1.ALD");
 *
 * The parser expects the raw XOR-encoded bytes from disk.
 * It applies _codifica() (XOR-0xFF) internally before parsing.
 */
class AldParser {
public:
	/**
	 * Parse a .ALD file from a stream into an AldScene.
	 *
	 * @param stream   Raw bytes from disk (XOR-0xFF encoded). Stream is read fully.
	 * @param scene    Output: populated scene descriptor.
	 * @return true on success, false on format error.
	 */
	bool parseScene(Common::SeekableReadStream &stream, AldScene &scene);

	/**
	 * Extract scene ID from .ALD filename (e.g. "10.ALD" → "10").
	 * Used to verify P$ field after parsing.
	 */
	static Common::String sceneIdFromFilename(const Common::String &filename);

private:
	/**
	 * Apply _codifica() — XOR each byte with 0xFF (bitwise NOT).
	 * Replicates the original HARE.EXE _codifica(char *buf, int len) exactly.
	 *
	 * @param data  Buffer to modify in place.
	 */
	static void _codifica(Common::Array<byte> &data);

	/**
	 * Split XOR-decoded bytes into CRLF-terminated lines.
	 * QuickBASIC PRINT #1 writes 0x0D 0x0A. After XOR-0xFF: 0xF2 0xF5.
	 * After _codifica() the bytes are plain ASCII with real \r\n.
	 *
	 * @param data   Decoded byte array.
	 * @return Array of trimmed, non-empty line strings.
	 */
	static Common::Array<Common::String> splitLines(const Common::Array<byte> &data);

	/**
	 * Parse a line as an integer. QuickBASIC PRINT #1 adds spaces: " 42 ".
	 * strip() + atoi() equivalent.
	 */
	static int16 parseInt(const Common::String &line);
};

} // namespace Paco1994

#endif // PACO1994_ALD_PARSER_H
