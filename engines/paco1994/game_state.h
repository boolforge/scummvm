/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * @file game_state.h
 * @brief Game state container — replicates all global variables from HARE.EXE BSS/data segment.
 *
 * In the original DOS binary, all of these are global C variables in the data/BSS segment.
 * The TDS symbol table (Borland TDS v2.08, magic 0xFB52) names each one explicitly.
 * DS offsets are estimated from TDS data; exact values require full disassembly.
 *
 * SAVE/LOAD (_graba_partida / _carga_partida):
 *   CORRECTED (see AUXILIO_format_analysis_CORRECTED.md, Finding 1):
 *   Direct byte inspection of three real save files (GAMESAVE, AUXILIO, AUXILIAR)
 *   confirms the on-disk format is 100% PLAIN ASCII TEXT, CRLF-delimited —
 *   NOT XOR-0xFF encoded. This differs from .ALD scene files, which ARE
 *   XOR-0xFF encoded (_codifica()). The two formats are not interchangeable;
 *   an earlier draft of this file incorrectly assumed XOR-0xFF applied here
 *   by analogy with .ALD. That assumption is retracted.
 *
 *   Confirmed structure (all three sample files share this header):
 *     Line 0: scene reference filename, e.g. "6.ALD"
 *     Line 1: integer, diverges per save (124 / 148 / 214 in samples) —
 *             meaning UNCONFIRMED without disassembly (possibly a move/tick counter)
 *     Line 2: integer, constant = 64 across all samples — meaning UNCONFIRMED
 *             (plausibly a fixed array-size constant baked into the save routine)
 *     Line 3+: integer array (flags/inventory state), variable length per save
 *              (29 entries in GAMESAVE/AUXILIO, 49 in AUXILIAR)
 *   No terminator sentinel was found; the array simply ends at EOF.
 *
 *   ScummVM port: synchronize() below must serialize as plain text matching
 *   this structure, NOT apply codifica()/XOR to the byte stream.
 *
 * PUZZLE FLAGS (_flags[] / _banderas[]):
 *   Original: char flags[32] in BSS. Indexed by integer constants.
 *   Port: named enum for clarity; mapped to the same flat index for save compat.
 */

#ifndef PACO1994_GAME_STATE_H
#define PACO1994_GAME_STATE_H

#include "common/array.h"
#include "common/point.h"
#include "common/serializer.h"
#include "common/str.h"

namespace Paco1994 {

// ---------------------------------------------------------------------------
// Puzzle flag indices — _flags[] array in HARE.EXE
// ---------------------------------------------------------------------------
// Named constants for the boolean _flags[]/char array.
// Original: int array indexed by literal constants in _comprueba/_comprueba1/_comprueba2.
// These indices must remain stable for save-game compatibility.

enum PacoFlag {
	kFlagDoorOpen       = 0,   ///< Scene 10→9: guard unblocked after book interaction
	kFlagSwitchUsed     = 1,   ///< Scene 10 obj#40: security switch activated
	kFlagMetMariano     = 2,   ///< Scene 11 obj#43: first contact with Mariano NPC
	kFlagGaveFood       = 3,   ///< Food given to Mariano (triggers book exchange)
	kFlagGotFood        = 4,   ///< Player obtained food item from vending machine
	kFlagGotBook        = 5,   ///< Book received from Mariano
	kFlagReadBook       = 6,   ///< Player examined the book (enables guard interaction)
	kFlagNpcHappy       = 7,   ///< Generic NPC satisfaction flag
	kFlagDoor2Open      = 8,   ///< Second guard passage unblocked
	kFlagNpcSatisfied1  = 9,   ///< First NPC food acceptance
	kFlagNpcSatisfied2  = 10,  ///< Second NPC food acceptance
	kFlagMax            = 10   ///< Confirmed exact via disassembly of _graba_partida's fprintf loop count (reko-decomp.txt:3936-3995)
};

// ---------------------------------------------------------------------------
// Inventory item IDs — _objetos_que_tengo[]
// ---------------------------------------------------------------------------
// Original: int objetos_que_tengo[MAX_ITEMS] — cells hold object IDs or 0.
// TDS symbol _num_obj tracks the count; _num_objd tracks disc-objects in scene.

enum ItemId {
	kItemNone  = 0,
	kItemFood  = 1,   ///< "Salchichas de humano" — from vending machine (_escoba adjacent?)
	kItemBook  = 2,   ///< Book about world peace — received from Mariano NPC
	kItemBroom = 3,   ///< _escoba (TDS symbol: _carga_escoba/_palanca) — broom/lever item
	kItemMax   = 16   ///< Confirmed exact via disassembly (the same save-routine loop count)
};

// ---------------------------------------------------------------------------
// Paco animation constants
// ---------------------------------------------------------------------------

/// Walk cycle frame count (_num_frame cycles 0–3 in _aumenta_num_frame)
static constexpr int kWalkFrameCount = 4;
/// Pixels per frame for _lleva_al_hare() linear interpolation (estimated)
static constexpr int kMoveStep       = 3;
/// Paco's default Y coordinate on scene entry (_hare_y initial value)
static constexpr int kDefaultHareY   = 170;
/// Maximum Y for walkable zone (below this = dialogue/HUD area at y=155–199)
static constexpr int kWalkZoneMaxY   = 148;

/// Entry X coordinates per PPOS value from .ALD door data (_sitio application)
inline int entryX(int ppos) {
	switch (ppos) {
	case 0: return 20;    ///< PPOS=0: left-side entry
	case 1: return 300;   ///< PPOS=1: right-side entry
	default: return 160;  ///< PPOS=2: center entry (default)
	}
}

// ---------------------------------------------------------------------------
// GameState — all mutable game variables
// ---------------------------------------------------------------------------

/**
 * Central game state. All fields correspond directly to named symbols in
 * the HARE.EXE TDS symbol table, or are derived from the ALD data structure.
 *
 * Thread safety: Not thread-safe. Original DOS game was single-threaded.
 * The ScummVM port runs in a single-threaded event loop matching the original.
 */
class GameState {
public:
	GameState();
	~GameState() = default;

	// ── Current scene (_num_room) ───────────────────────────────────────────
	/// Scene ID string matching P$ in .ALD file ("1", "10", "11", ...)
	Common::String currentScene;

	// ── Paco character state ────────────────────────────────────────────────
	int16  hareX;        ///< _hare_x — pixel X coordinate (0-319)
	int16  hareY;        ///< _hare_y — pixel Y coordinate (0-199)
	bool   hareMoving;   ///< _hare_se_mueve: true while walking to target
	int8   hareFacing;   ///< _sentido_hare: 0=left, 1=right
	int8   frameIndex;   ///< _num_frame: walk cycle frame (0–3)
	int8   frameTimer;   ///< frames elapsed at current animation frame (not in TDS)
	int16  targetX;      ///< walk target X (from _lleva_al_hare)
	int16  targetY;      ///< walk target Y

	// ── Puzzle flags (_flags[] / _banderas[]) ─────────────────────────────
	bool   flags[kFlagMax];

	// ── Inventory (_objetos_que_tengo[], _num_obj) ─────────────────────────
	int    inventory[kItemMax];  ///< item ID per slot, 0=empty
	int8   numItems;             ///< _num_obj — occupied slots

	// ── Active dialogue (_hablar state) ────────────────────────────────────
	Common::String dialogueText;   ///< Currently displayed text (empty = no dialogue)
	Common::String dialogueAudio;  ///< Currently playing .ALS filename (empty = none)
	bool           dialogueActive; ///< True while player must click to dismiss

	// ── Music state (_musica_room / _musica_antes) ─────────────────────────
	Common::String currentMusic;   ///< _musica_room — active CMF filename
	Common::String prevMusic;      ///< _musica_antes — previous room's music

	// ── Convenience flag accessors ──────────────────────────────────────────
	bool getFlag(PacoFlag f) const { return (f < kFlagMax) && flags[f]; }
	void setFlag(PacoFlag f, bool v = true) { if (f < kFlagMax) flags[f] = v; }

	// ── Inventory helpers ───────────────────────────────────────────────────
	bool hasItem(ItemId item) const;
	void addItem(ItemId item);    ///< _coge_objeto(obj_id)
	void removeItem(ItemId item); ///< _saca_objeto(obj_id)

	// ── Scene transition setup (_al_a_pantalla_k_eva) ──────────────────────
	/**
	 * Apply the PPOS entry position to Paco's X coordinate.
	 * Called immediately after loading a new scene when entering via a door.
	 * Replicates the _sitio application in _al_a_pantalla_k_eva().
	 */
	void applyEntryPos(int ppos);

	// ── Dialogue helpers ────────────────────────────────────────────────────
	void showDialogue(const Common::String &text, const Common::String &alsFile = "");
	void clearDialogue();

	// ── Serialization (_graba_partida / _carga_partida with XOR-0xFF) ──────
	/**
	 * Serialize state to/from a ScummVM save stream.
	 *
	 * CORRECTED: does NOT apply XOR-0xFF. Verified against three real save
	 * files (GAMESAVE, AUXILIO, AUXILIAR) — the on-disk format is plain
	 * CRLF-delimited ASCII text. See AUXILIO_format_analysis_CORRECTED.md.
	 * An earlier draft of this method incorrectly XOR-encoded the stream
	 * by false analogy with the (genuinely XOR-0xFF encoded) .ALD format.
	 *
	 * @param sz  Serializer in save or load mode.
	 */
	void synchronize(Common::Serializer &sz);
};

} // namespace Paco1994

#endif // PACO1994_GAME_STATE_H
