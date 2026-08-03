/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "paco1994/interaction.h"
#include "paco1994/paco1994.h"
#include "paco1994/game_state.h"

#include "common/debug.h"
#include "common/str.h"

namespace Paco1994 {

// ═══════════════════════════════════════════════════════════════════════════
// DIALOGUE DATABASE — all strings from HARE.EXE data section (0xC182-0xC60E)
// ═══════════════════════════════════════════════════════════════════════════
//
// Format: { textES, textEN, alsFile, flagSet, addItem, removeItem, hexOffset }
// alsFile: nullptr = no audio for this dialogue line.
// flagSet: kFlagMax = no flag mutation.
// addItem / removeItem: kItemNone = no inventory change.
//
// LOCALIZATION NOTE: ScummVM's translation workflow will replace textEN with
// the user's preferred language from POTFILES. textES is always the canonical
// reference (original game language). Translate with _("...") macro.
//
const DialogueEntry kDialogues[kDlgMax] = {
	// kDlgBlockedDoor — guard refuses entry at Scene 10 → Scene 9
	// HARE.EXE offset 0xC182 | Audio: 9.ALS (not in demo)
	{
		"Lo siento, no te puedo dejar pasar.",
		"Sorry, I can't let you through.",
		"9.ALS", kFlagMax, kItemNone, kItemNone, 0xC182
	},
	// kDlgStuckDoor — stuck door in scenes 4-9 (assets missing from demo)
	// HARE.EXE offset 0xC1AC | Audio: 3.ALS (3.56s, available)
	{
		"Esta atascada. Necesitare algo para abrirla.",
		"It's stuck. I'll need something to open it.",
		"3.ALS", kFlagMax, kItemNone, kItemNone, 0xC1AC
	},
	// kDlgSecurityDoor — security door interruptor (Scene 10, obj#40, first click)
	// HARE.EXE offset 0xC1DF | Audio: 8.ALS (not in demo)
	{
		"Es una puerta de seguiridad. Debe haber un interruptor en algun sitio.",
		"It's a security door. There must be a switch somewhere around here.",
		"8.ALS", kFlagSwitchUsed, kItemNone, kItemNone, 0xC1DF
	},
	// kDlgVendingMachine — human sausage vending machine (disc-objects in scenes 2/3)
	// HARE.EXE offset 0xC22C | Audio: 1.ALS (3.52s, available)
	{
		"Es una maquina expendedora de salchichas de humano.",
		"It's a human sausage vending machine.",
		"1.ALS", kFlagGotFood, kItemFood, kItemNone, 0xC22C
	},
	// kDlgSlotLook — slot machine first observation (no audio)
	// HARE.EXE offset 0xC267
	{
		"Al loro! ",
		"Heads up!",
		nullptr, kFlagMax, kItemNone, kItemNone, 0xC267
	},
	// kDlgSlotUse — slot machine use trigger (7.ALS not in demo)
	// HARE.EXE offset 0xC271 | Audio: 7.ALS
	{
		"Una maquina tragaperras!",
		"A slot machine!",
		"7.ALS", kFlagMax, kItemNone, kItemNone, 0xC271
	},
	// kDlgPacoGreetsMariano — Paco's greeting (no audio — Paco's half)
	// HARE.EXE offset 0xC291
	{
		"Que te pasa amigo mio, que te veo desolado?",
		"What's the matter, my friend? You look so down.",
		nullptr, kFlagMetMariano, kItemNone, kItemNone, 0xC291
	},
	// kDlgMarianoSad — Mariano's sad response (5.ALS not in demo)
	// HARE.EXE offset 0xC2C3 | Audio: 5.ALS
	{
		"estoy triste porque mi vida no tiene sentido. y porque tengo hambre",
		"I'm sad because my life has no meaning. And because I'm hungry.",
		"5.ALS", kFlagMax, kItemNone, kItemNone, 0xC2C3
	},
	// kDlgMarianoAmbience — 40-space blank string + audio-only pause
	// HARE.EXE offset 0xC30E | Audio: 10.ALS (4.95s, available)
	// textES = 40 ASCII spaces (confirmed from binary scan at 0xC30E)
	{
		"                                        ",
		"",
		"10.ALS", kFlagMax, kItemNone, kItemNone, 0xC30E
	},
	// kDlgMarianoBook — Mariano gives book (requires food in inventory)
	// HARE.EXE offset 0xC481 | Audio: 2.ALS (1.93s, available)
	// Side effects: removes food, adds book (handled in Scene11Handler)
	{
		"Tu ten karma, y leete este libro.",
		"You keep the karma, and read this book.",
		"2.ALS", kFlagGaveFood, kItemBook, kItemFood, 0xC481
	},
	// kDlgPacoReadsBook — Paco reads the received book
	// HARE.EXE offset 0xC4AA | Audio: 2.ALS (reused, 1.93s)
	{
		"OH! En este libro aparecen nuevas teorias sobre donde esta la paz en el mundo! ",
		"OH! This book contains new theories about where world peace can be found!",
		"2.ALS", kFlagReadBook, kItemNone, kItemNone, 0xC4AA
	},
	// kDlgThanks — generic NPC gratitude (no audio)
	// HARE.EXE offset 0xC4F3
	{
		"muchas gracias!",
		"Thank you very much!",
		nullptr, kFlagMax, kItemNone, kItemNone, 0xC4F3
	},
	// kDlgGuardPass1 — guard unblocked (primary) — LONGEST audio: 8.02s
	// HARE.EXE offset 0xC512 | Audio: 13.ALS (8.02s, available)
	// This fires when player has book and clicks blocked door (obj#39, scene 10)
	{
		"Has demostrado ser un gran amigo. Puedes pasar si quieres.",
		"You've proven yourself to be a great friend. You may pass if you like.",
		"13.ALS", kFlagDoorOpen, kItemNone, kItemNone, 0xC512
	},
	// kDlgGiveFood1 — "Toma machote" instance 1 (14.ALS, 3.83s)
	// "Machote" = Spanish colloquial: "big guy / buddy"
	// HARE.EXE offset 0xC554 | Audio: 14.ALS (available)
	{
		"Toma machote.",
		"Here you go, pal.",
		"14.ALS", kFlagNpcSatisfied1, kItemNone, kItemNone, 0xC554
	},
	// kDlgRefuseFood — NPC refuses food (wrong NPC or wrong state)
	// HARE.EXE offset 0xC568 | Audio: 6.ALS (not in demo)
	// "Chopped" = Spanish brand of canned processed meat (like Spam)
	{
		"No gracias. No me gusta el chopped",
		"No thanks. I don't like processed meat.",
		"6.ALS", kFlagMax, kItemNone, kItemNone, 0xC568
	},
	// kDlgGiveFood2 — "Toma machote" instance 2, different audio (11.ALS)
	// HARE.EXE offset 0xC592 | Audio: 11.ALS (2.31s, available)
	{
		"Toma machote.",
		"Here you go, pal.",
		"11.ALS", kFlagNpcSatisfied2, kItemNone, kItemNone, 0xC592
	},
	// kDlgAcceptFood — NPC accepts food (6.ALS reused)
	// HARE.EXE offset 0xC5A6 | Audio: 6.ALS (not in demo)
	{
		"Gracias. Enseguida me lo como.",
		"Thanks. I'll eat it right away.",
		"6.ALS", kFlagNpcHappy, kItemNone, kItemNone, 0xC5A6
	},
	// kDlgGuardPass2 — guard unblocked (secondary / shorter: 1.75s)
	// HARE.EXE offset 0xC5CC | Audio: 12.ALS (available)
	{
		"Has demostrado ser un gran amigo. Puedes pasar si quieres.",
		"You've proven yourself to be a great friend. You may pass if you like.",
		"12.ALS", kFlagDoor2Open, kItemNone, kItemNone, 0xC5CC
	},
	// kDlgGiveFoodFinal — "Toma machote" instance 3 (14.ALS reused)
	// HARE.EXE offset 0xC60E | Audio: 14.ALS (available)
	{
		"Toma machote.",
		"Here you go, pal.",
		"14.ALS", kFlagMax, kItemNone, kItemNone, 0xC60E
	},
	// kDlgSaveDone — save game confirmation (no audio)
	// HARE.EXE offset 0xC3F4 | "Tio" = Spanish slang: "dude/man"
	{
		"Acabas de grabar la partida, tio.",
		"You just saved your game, dude.",
		nullptr, kFlagMax, kItemNone, kItemNone, 0xC3F4
	},
};

// ═══════════════════════════════════════════════════════════════════════════
// Helper: fire a dialogue entry (text + audio + state mutations)
// ═══════════════════════════════════════════════════════════════════════════
static void fireDialogue(DialogueKey key, GameState &state, Paco1994Engine &eng) {
	const DialogueEntry &e = kDialogues[key];

	// Apply inventory mutations BEFORE showing text (matches original order)
	if (e.removeItem != kItemNone)
		state.removeItem(e.removeItem);
	if (e.addItem != kItemNone)
		state.addItem(e.addItem);
	if (e.flagSet != kFlagMax)
		state.setFlag(e.flagSet, true);

	// Show text in dialogue box + play VOC audio
	// Engine::showDialogue() calls _hablar() equivalent:
	//   - draw black box at y=155-199 (_centra_texto boundary)
	//   - render centered text (_centra_texto)
	//   - start VOC playback via makeVOCStream() if alsFile != nullptr
	//   - set state.dialogueActive = true (await click to dismiss)
	eng.showDialogue(e.textEN, e.alsFile ? e.alsFile : "");

	debugC(1, kDebugInteract, "fireDialogue: key=%d text='%.40s' als=%s flag=%d",
	       (int)key, e.textEN, e.alsFile ? e.alsFile : "none", (int)e.flagSet);
}

// ═══════════════════════════════════════════════════════════════════════════
// DefaultSceneHandler
// ═══════════════════════════════════════════════════════════════════════════
void DefaultSceneHandler::handleObject(int16 objId, GameState &state, Paco1994Engine &eng) {
	// No special object interactions in default scenes (1, 4-9 inferred).
	// Paco comments generically if implementation is incomplete.
	debugC(1, kDebugInteract, "DefaultScene: unhandled object id=%d in scene '%s'",
	       objId, state.currentScene.c_str());
}

void DefaultSceneHandler::handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) {
	// Default door handler: unconditional scene transition.
	// No puzzle checks — just navigate to destination.
	// _al_a_pantalla_k_eva(dest_scene, dest_door, dest_pos)
	eng.transitionToScene(obj.destScene, obj.destDoorId, obj.destPos);
}

// ═══════════════════════════════════════════════════════════════════════════
// Scene2_3Handler — vending machine interaction
// ═══════════════════════════════════════════════════════════════════════════
void Scene2_3Handler::handleObject(int16 objId, GameState &state, Paco1994Engine &eng) {
	// Disc-object sprite IDs in scenes 2 and 3:
	//   Scene 2: sprite#4 at sheet(125,17,164,56) → scene(195,41)
	//   Scene 3: sprite#3 at sheet(84,17,123,56)  → scene(154,78)
	// Both represent item sources (vending machine / "maquina expendedora").
	// ALD object IDs for disc-objects are their NUOD(I) + scene offset.
	// In _comprueba1/_comprueba2, the click is detected via the disc-object
	// bounding box, not a hotspot rect. We use the ALD disc-object place coords
	// + kTrueHeight/kWidth as the effective hotspot. InteractionDispatcher
	// handles this mapping — here we assume the dispatch already resolved objId.
	if (!state.getFlag(kFlagGotFood) && !state.hasItem(kItemFood)) {
		fireDialogue(kDlgVendingMachine, state, eng);
	} else {
		// Item already obtained: generic "you already have it" response
		eng.showDialogue("You already have enough food.", "");
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// Scene10Handler
// ═══════════════════════════════════════════════════════════════════════════
void Scene10Handler::handleObject(int16 objId, GameState &state, Paco1994Engine &eng) {
	if (objId == 40) {
		// Security switch / interruptor (OBJECT, not door)
		// First interaction: Paco comments on the security door
		// Subsequent: switch already noted, no repeat dialogue
		if (!state.getFlag(kFlagSwitchUsed)) {
			fireDialogue(kDlgSecurityDoor, state, eng); // sets kFlagSwitchUsed
		} else {
			eng.showDialogue("The switch has already been activated.", "");
		}
	} else {
		debugC(1, kDebugInteract, "Scene10: unhandled object id=%d", objId);
	}
}

void Scene10Handler::handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) {
	if (obj.id == 39) {
		// Door obj#39 → Scene 9, door#38, pos=LEFT
		// BLOCKED until kFlagDoorOpen is set by guard interaction
		if (state.getFlag(kFlagDoorOpen)) {
			// Unlocked: navigate normally
			eng.transitionToScene(obj.destScene, obj.destDoorId, obj.destPos);
		} else if (state.hasItem(kItemBook) && state.getFlag(kFlagReadBook)) {
			// Player has the book and has read it: guard relents
			// This fires the LONGEST audio in the game (13.ALS, 8.02s)
			fireDialogue(kDlgGuardPass1, state, eng); // sets kFlagDoorOpen
			// Navigation happens on next click (after dialogue dismissed)
		} else {
			// Guard simply refuses — DLG001
			fireDialogue(kDlgBlockedDoor, state, eng);
		}
	} else if (obj.id == 41) {
		// Door obj#41 → Scene 11, door#42, pos=RIGHT — always passable
		eng.transitionToScene(obj.destScene, obj.destDoorId, obj.destPos);
	} else {
		DefaultSceneHandler::handleDoor(obj, state, eng);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// Scene11Handler — Mariano NPC progressive dialogue state machine
// ═══════════════════════════════════════════════════════════════════════════
void Scene11Handler::handleObject(int16 objId, GameState &state, Paco1994Engine &eng) {
	if (objId == 43) {
		// Mariano NPC (large hotspot: rect 34,35 to 140,109)
		// Progressive dialogue state machine — _habla_mariano() in original
		if (!state.getFlag(kFlagMetMariano)) {
			// First meeting: Paco asks → Mariano responds (sad + hungry)
			// Original fires both kDlgPacoGreetsMariano then kDlgMarianoSad
			// sequentially. We chain them via engine dialogue queue.
			fireDialogue(kDlgPacoGreetsMariano, state, eng); // sets kFlagMetMariano
			// Queue Mariano's sad response immediately after
			eng.queueDialogue(kDlgMarianoSad);
			eng.queueDialogue(kDlgMarianoAmbience); // 4.95s audio-only pause

		} else if (state.hasItem(kItemFood) && !state.getFlag(kFlagGaveFood)) {
			// Player has food + hasn't given it yet → exchange
			// fireDialogue handles: removeItem(food), addItem(book), setFlag(kFlagGaveFood)
			fireDialogue(kDlgMarianoBook, state, eng);

		} else if (state.hasItem(kItemBook) && !state.getFlag(kFlagReadBook)) {
			// Player examines the received book
			fireDialogue(kDlgPacoReadsBook, state, eng); // sets kFlagReadBook

		} else {
			// Repeat: Paco re-greets Mariano
			fireDialogue(kDlgPacoGreetsMariano, state, eng);
		}
	} else {
		debugC(1, kDebugInteract, "Scene11: unhandled object id=%d", objId);
	}
}

void Scene11Handler::handleDoor(const AldObject &obj, GameState &state, Paco1994Engine &eng) {
	if (obj.id == 42) {
		// Door obj#42 → Scene 10, door#41, pos=LEFT — always passable
		eng.transitionToScene(obj.destScene, obj.destDoorId, obj.destPos);
	} else {
		DefaultSceneHandler::handleDoor(obj, state, eng);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
// InteractionDispatcher
// ═══════════════════════════════════════════════════════════════════════════
InteractionDispatcher::InteractionDispatcher()
	: _handler(nullptr) {}

IInteractionHandler *InteractionDispatcher::createHandler(const Common::String &sceneId) {
	if (sceneId == "10")
		return new Scene10Handler();
	if (sceneId == "11")
		return new Scene11Handler();
	if (sceneId == "2" || sceneId == "3")
		return new Scene2_3Handler();
	// All other scenes (1, 4-9): default handler (doors only)
	return new DefaultSceneHandler();
}

void InteractionDispatcher::setScene(const Common::String &sceneId) {
	delete _handler;
	_handler  = createHandler(sceneId);
	_sceneId  = sceneId;
	debugC(1, kDebugInteract, "InteractionDispatcher: scene '%s' → %s",
	       sceneId.c_str(), _handler ? "handler created" : "nullptr (ERROR)");
}

bool InteractionDispatcher::dispatchClick(int16 x, int16 y,
                                          const AldScene &scene,
                                          GameState &state,
                                          Paco1994Engine &eng) {
	if (!_handler) {
		warning("InteractionDispatcher: no handler for scene '%s'", _sceneId.c_str());
		return false;
	}

	// Hit-test all ALD on-screen objects in order (matches original _comprueba loop)
	// First match wins — objects are listed in ALD order from GRABAR.
	for (uint i = 0; i < scene.objects.size(); i++) {
		const AldObject &obj = scene.objects[i];

		// Rect::contains() checks x1<=x<x2, y1<=y<y2 (half-open interval)
		if (!obj.rect.contains(x, y))
			continue;

		debugC(1, kDebugInteract, "Click (%d,%d) hit object id=%d isDoor=%d",
		       x, y, obj.id, (int)obj.isDoor);

		if (obj.isDoor)
			_handler->handleDoor(obj, state, eng);
		else
			_handler->handleObject(obj.id, state, eng);

		return true; // hotspot hit
	}

	// No hotspot hit: Paco walks to click position
	// _lleva_al_hare(x, y) — handled by engine's movement system
	return false;
}

} // namespace Paco1994
