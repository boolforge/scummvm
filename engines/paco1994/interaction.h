/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * @file interaction.h
 * @brief Hardcoded interaction logic — replicates _comprueba/_comprueba1/_comprueba2 from HARE.EXE.
 *
 * ARCHITECTURAL NOTE — WHY THIS IS HARDCODED:
 *   The HARE.EXE binary contains NO script interpreter, NO bytecode VM, and NO external
 *   narrative script files. All puzzle logic, dialogue triggers, and item interactions are
 *   baked as C if/else chains in the compiled binary.
 *
 *   The existing engines/alcachofa engine has a full Script VM (script.h: ScriptOp enum,
 *   30+ opcodes, procedureCount, behaviorCount). That engine is for the LATE-1990s 3D
 *   Mortadelo y Filemón Windows games. It is ARCHITECTURALLY INCOMPATIBLE with this 1994 title.
 *
 *   This file reimplements the hardcoded logic in the "Siloed Logic" pattern from the wiki
 *   article: each scene's interaction context is isolated in its own handler class
 *   implementing IInteractionHandler.
 *
 * DIALOGUE STRINGS:
 *   All 19 dialogue strings are hardcoded in the HARE.EXE data section.
 *   Extracted from offsets 0xC182–0xC60E via binary analysis.
 *   Each string is a C string literal in the original; we store them in the
 *   DialogueDatabase struct below.
 *
 * LOCALIZATION:
 *   The DialogueEntry has both ES (original Spanish) and EN-US (translated).
 *   ScummVM's translation system maps these via the POTFILES translation workflow.
 *   A companion spreadsheet (Paco_Hare_1994_Localization_and_Symbols.xlsx) provides
 *   full 6-language translations (ES/EN-US/FR/DE/IT/PT-BR).
 */

#ifndef PACO1994_INTERACTION_H
#define PACO1994_INTERACTION_H

#include "paco1994/ald_parser.h"
#include "paco1994/game_state.h"

namespace Paco1994 {

class Paco1994Engine;

// ---------------------------------------------------------------------------
// Dialogue Database
// ---------------------------------------------------------------------------

/**
 * One entry from HARE.EXE data section.
 * Original: C string literals adjacent in the .data segment.
 * Verified offsets from binary analysis of HARE.EXE.
 *
 * alsFile: Creative Labs VOC v1.10, PCM 8-bit unsigned, mono, 21739 Hz.
 *          NULL = no audio for this line.
 * flagSet: PacoFlag to assert after this dialogue fires. kFlagMax = none.
 * addItem: Item to add to inventory after this dialogue. kItemNone = none.
 * removeItem: Item to remove from inventory. kItemNone = none.
 */
struct DialogueEntry {
	const char  *textES;       ///< Original Spanish text (from HARE.EXE)
	const char  *textEN;       ///< English translation
	const char  *alsFile;      ///< VOC audio filename (NULL = no audio)
	PacoFlag     flagSet;      ///< Flag to set after firing (kFlagMax = none)
	ItemId       addItem;      ///< Item to add (kItemNone = none)
	ItemId       removeItem;   ///< Item to remove (kItemNone = none)
	uint32       hexOffset;    ///< Hex offset of textES in HARE.EXE (debug info)
};

/**
 * Lookup key for the dialogue database.
 * Used by handlers to retrieve text + audio + state mutations atomically.
 */
enum DialogueKey {
	// Guard at Scene 10 → Scene 9 door (obj#39) — blocked dialogue
	kDlgBlockedDoor,      // 0xC182 "Lo siento, no te puedo dejar pasar."
	// Stuck door in intermediate scenes (4-9)
	kDlgStuckDoor,        // 0xC1AC "Esta atascada. Necesitare algo para abrirla."
	// Security door / interruptor (Scene 10, obj#40)
	kDlgSecurityDoor,     // 0xC1DF "Es una puerta de seguiridad. Debe haber un interruptor..."
	// Vending machine (disc-objects in scenes 2/3)
	kDlgVendingMachine,   // 0xC22C "Es una maquina expendedora de salchichas de humano."
	// Slot machine — first look (no audio)
	kDlgSlotLook,         // 0xC267 "Al loro!"
	// Slot machine — use
	kDlgSlotUse,          // 0xC271 "Una maquina tragaperras!"
	// Paco greets Mariano (no audio — Paco's side of conversation)
	kDlgPacoGreetsMariano, // 0xC291 "Que te pasa amigo mio, que te veo desolado?"
	// Mariano's sad response (audio: 5.ALS — not in demo)
	kDlgMarianoSad,       // 0xC2C3 "estoy triste porque mi vida no tiene sentido..."
	// Audio-only ambient pause (10.ALS, 4.95s, 40-space blank text)
	kDlgMarianoAmbience,  // 0xC30E [40 spaces]
	// Mariano gives book to Paco
	kDlgMarianoBook,      // 0xC481 "Tu ten karma, y leete este libro."
	// Paco reads the book
	kDlgPacoReadsBook,    // 0xC4AA "OH! En este libro aparecen nuevas teorias..."
	// Generic thanks (no audio)
	kDlgThanks,           // 0xC4F3 "muchas gracias!"
	// Guard unblocked — primary (13.ALS, 8.02s)
	kDlgGuardPass1,       // 0xC512 "Has demostrado ser un gran amigo. Puedes pasar si quieres."
	// Give food to NPC — first instance (14.ALS, 3.83s)
	kDlgGiveFood1,        // 0xC554 "Toma machote."
	// NPC refuses food (6.ALS — not in demo)
	kDlgRefuseFood,       // 0xC568 "No gracias. No me gusta el chopped"
	// Give food to NPC — second instance (11.ALS, 2.31s)
	kDlgGiveFood2,        // 0xC592 "Toma machote."
	// NPC accepts food (6.ALS — not in demo)
	kDlgAcceptFood,       // 0xC5A6 "Gracias. Enseguida me lo como."
	// Guard unblocked — secondary (12.ALS, 1.75s)
	kDlgGuardPass2,       // 0xC5CC "Has demostrado ser un gran amigo. Puedes pasar si quieres."
	// Give food — final instance (14.ALS, 3.83s)
	kDlgGiveFoodFinal,    // 0xC60E "Toma machote."
	// Save confirmation (no audio)
	kDlgSaveDone,         // 0xC3F4 "Acabas de grabar la partida, tio."
	kDlgMax
};

/// The complete dialogue database. All strings extracted from HARE.EXE binary.
extern const DialogueEntry kDialogues[kDlgMax];

// ---------------------------------------------------------------------------
// IInteractionHandler — interface for per-scene interaction logic
// ---------------------------------------------------------------------------

/**
 * "Siloed Logic" pattern: each scene gets its own IInteractionHandler subclass.
 * This dismantles the monolithic switch/case in _comprueba/_comprueba1/_comprueba2
 * into isolated, testable per-room contexts.
 *
 * The pattern is analogous to how ScummVM's SCUMM engine implements per-script
 * opcodes but without a bytecode VM — each handler IS the logic for that scene.
 */
class IInteractionHandler {
public:
	virtual ~IInteractionHandler() {}

	/**
	 * Handle a click on a non-door interactive object (PUER=0).
	 * Replicates _comprueba1() / _comprueba2() per-object case in HARE.EXE.
	 *
	 * @param objId  ALD object ID (NUM field)
	 * @param state  Mutable game state
	 * @param eng    Engine instance (for dialogue/audio dispatch)
	 */
	virtual void handleObject(int16 objId, GameState &state, Paco1994Engine &eng) = 0;

	/**
	 * Handle a click on a door/exit (PUER=1).
	 * Checks flag conditions and either blocks or transitions.
	 * Replicates the door dispatch in _comprueba() → _al_a_pantalla_k_eva().
	 *
	 * @param obj    ALD door object (contains dest scene, door ID, pos)
	 * @param state  Mutable game state
	 * @param eng    Engine instance (for scene transition and dialogue)
	 */
	virtual void handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) = 0;
};

// ---------------------------------------------------------------------------
// Per-scene handler implementations
// ---------------------------------------------------------------------------

/**
 * Default handler for scenes with no special object interactions.
 * Only handles door navigation (no puzzles). Used for scenes 1, 2, 3
 * where the only interactions are the two exit doors per scene.
 */
class DefaultSceneHandler : public IInteractionHandler {
public:
	void handleObject(int16 objId, GameState &state, Paco1994Engine &eng) override;
	void handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) override;
};

/**
 * Scene 2 + Scene 3 handler: disc-object interaction (vending machine).
 * Sprite#4 in scene 2 and sprite#3 in scene 3 can be clicked to obtain food.
 * Replicates _comprueba1() case for disc-object IDs 3 and 4 in HARE.EXE.
 */
class Scene2_3Handler : public DefaultSceneHandler {
public:
	void handleObject(int16 objId, GameState &state, Paco1994Engine &eng) override;
};

/**
 * Scene 10 handler — security switch + blocked door.
 *
 * Objects in Scene 10 (from 10.ALD):
 *   obj#39: DOOR → 9.ALD, door#38, pos=LEFT  [BLOCKED until kFlagDoorOpen]
 *   obj#40: OBJECT (security switch / interruptor)
 *   obj#41: DOOR → 11.ALD, door#42, pos=RIGHT [always passable]
 *
 * Replicates _comprueba() door dispatch for obj#39 + _comprueba1() for obj#40.
 */
class Scene10Handler : public IInteractionHandler {
public:
	void handleObject(int16 objId, GameState &state, Paco1994Engine &eng) override;
	void handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) override;
};

/**
 * Scene 11 handler — Mariano NPC progressive dialogue chain.
 *
 * Objects in Scene 11 (from 11.ALD):
 *   obj#42: DOOR → 10.ALD, door#41, pos=LEFT  [always passable]
 *   obj#43: OBJECT (Mariano NPC — large hotspot 34,35 to 140,109)
 *
 * NPC dialogue state machine (keyed on _flags[]):
 *   !kFlagMetMariano           → DLG: kDlgMarianoSad    + set kFlagMetMariano
 *   kFlagMetMariano + has(food)
 *     + !kFlagGaveFood         → DLG: kDlgMarianoBook   + set kFlagGaveFood
 *                                     + removeItem(food) + addItem(book)
 *   has(book) + !kFlagReadBook → DLG: kDlgPacoReadsBook + set kFlagReadBook
 *   else                       → DLG: kDlgPacoGreetsMariano  [repeat]
 *
 * Replicates _habla_mariano() + the conditional in _comprueba1() for obj#43.
 */
class Scene11Handler : public IInteractionHandler {
public:
	void handleObject(int16 objId, GameState &state, Paco1994Engine &eng) override;
	void handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) override;
};

// ---------------------------------------------------------------------------
// InteractionDispatcher — maps scene ID → IInteractionHandler
// ---------------------------------------------------------------------------

/**
 * Central dispatcher: creates the correct handler for the current scene
 * and routes click events from the game loop.
 *
 * Owns the handler instances; recreated on scene transition.
 */
class InteractionDispatcher {
public:
	InteractionDispatcher();

	/**
	 * Set the active scene. Creates the appropriate handler.
	 * Called by the engine on every scene transition.
	 *
	 * @param sceneId  ALD scene identifier string ("1", "10", "11", etc.)
	 */
	void setScene(const Common::String &sceneId);

	/**
	 * Route a click at (x, y) in 320×200 native coords.
	 * Hit-tests all ALD objects. Dispatches to current handler.
	 * Replicates the full _comprueba(x, y) → handler chain.
	 *
	 * @param x, y    Click coordinates in 320×200 pixel space
	 * @param scene   Parsed ALD data for current scene
	 * @param state   Mutable game state
	 * @param eng     Engine instance
	 * @return true if a hotspot was hit
	 */
	bool dispatchClick(int16 x, int16 y,
	                   const AldScene &scene,
	                   GameState &state,
	                   Paco1994Engine &eng);

private:
	IInteractionHandler *_handler; ///< Current scene's handler (owned)
	Common::String       _sceneId; ///< Active scene ID

	/// Factory: creates handler for given scene ID.
	static IInteractionHandler *createHandler(const Common::String &sceneId);
};

} // namespace Paco1994

#endif // PACO1994_INTERACTION_H
