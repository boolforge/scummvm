/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

/**
 * @file paco1994.h
 * @brief Main engine class for Paco El Hare vs Los Marcianos Siderales (1994).
 *
 * ARCHITECTURE OVERVIEW:
 *   HYBRID engine: spatial data is DATA-DRIVEN (.ALD files), narrative logic
 *   is HARDCODED (C if/else chains in HARE.EXE). No script VM exists.
 *
 * SCUMMVM APIS REUSED (zero code from engines/alcachofa which is 3D/OpenGL):
 *   Image::PCXDecoder  — .ALG ZSoft PCX v3.0 backgrounds (image/pcx.h)
 *   Audio::makeVOCStream — .ALS Creative VOC 1.10 dialogue (audio/decoders/voc.h)
 *   OPL::OPL           — CMF OPL2 FM music (audio/fmopl.h)
 *   Common::File       — all file I/O
 *   Graphics::Surface  — 320×200 back-buffer (equivalent to farmalloc(64000L))
 *   Common::SaveFileManager — gamesave with XOR-0xFF encoding
 *
 * BINARY TABLE @ 0xC630 (7×16 bytes, partially decoded):
 *   Contains non-0xFF values that are dialogue indices (0–17) plus unknown
 *   values 19, 20, 21, 22, 35. Row 6 = `00 10 00...` (possible init state).
 *   Full decoding requires disassembly of _comprueba/_comprueba1/_comprueba2.
 *   Stored as kObjectActionTable[] in dialogue.cpp for future analysis.
 *
 * DIALOGUE TABLE (19 entries, empirically verified from HARE.EXE 0xC182-0xC624):
 *   Format: [text\0][als_file\0] — text FIRST, ALS filename SECOND.
 *   Text encoding: DOS CP437 (¡=0xAD, ¿=0xA8). ScummVM uses UTF-8 internally.
 *   Entry 17: auxilio special — [text='auxilio'][bg='98.alg'][text='auxilio']
 *   Represents a GAME-OVER / PANIC SCREEN (not a normal dialogue).
 *
 * AUXILIO SCREEN:
 *   At 0xC60E: auxilio\0 98.alg\0 auxilio\0
 *   Loads 98.alg as background, displays "¡auxilio!" ("Help!"), no audio.
 *   Exact trigger conditions TBD (requires _comprueba disassembly).
 */

#ifndef PACO1994_ENGINE_H
#define PACO1994_ENGINE_H

#include "engines/engine.h"
#include "common/error.h"
#include "common/events.h"
#include "common/file.h"
#include "common/rect.h"
#include "common/serializer.h"
#include "common/str.h"
#include "audio/mixer.h"
#include "graphics/surface.h"

#include "paco1994/detection.h"
#include "paco1994/ald_parser.h"
#include "paco1994/game_state.h"
#include "paco1994/interaction.h"

// Forward declarations
namespace OPL { class OPL; }
namespace Audio { class SeekableAudioStream; }
namespace Image { class PCXDecoder; }

namespace Paco1994 {

/// Native VGA Mode 13h resolution (INT 10h AX=0013h)
static constexpr int kNativeW = 320;
static constexpr int kNativeH = 200;

/// Dialogue box position: y=155 to y=199 (45px strip at bottom)
/// Matches original _centra_texto(160, text) area in HARE.EXE
static constexpr int kDlgBoxY = 155;
static constexpr int kDlgBoxH = 45;

/// Walk zone: Paco cannot walk below y=148 (dialogue/HUD area below)
static constexpr int kWalkMaxY = 148;

/// VGA DAC shift: PCX palette stores 0-255, DAC accepts 0-63
/// ScummVM Graphics::Surface uses 0-255 natively — shift NOT applied here
static constexpr int kPaletteShift = 2; // used only for reference

/**
 * Paco1994Engine — the main engine instance.
 *
 * Lifetime: created by the MetaEngine, runs until game exits.
 * All subsystems are owned here.
 */
class Paco1994Engine : public Engine {
public:
	Paco1994Engine(OSystem *syst, const Paco1994GameDescription *gameDesc);
	~Paco1994Engine() override;

	// ── Engine interface ──────────────────────────────────────────────────
	Common::Error run() override;

	bool hasFeature(EngineFeature f) const override;
	bool canLoadGameStateCurrently(Common::U32String *msg = nullptr) override;
	bool canSaveGameStateCurrently(Common::U32String *msg = nullptr) override;
	Common::Error loadGameStream(Common::SeekableReadStream *stream) override;
	Common::Error saveGameStream(Common::WriteStream *stream, bool isAutosave = false) override;

	// ── Public API (called by interaction handlers) ───────────────────────

	/**
	 * Display text in the dialogue box and optionally play VOC audio.
	 * Equivalent to _hablar(char *text, char *als_file) in HARE.EXE.
	 *
	 * Implementation:
	 *   1. Draw black rectangle at (0, kDlgBoxY, 320, kDlgBoxH)
	 *   2. Render centered text via _centra_texto logic
	 *   3. If alsFile non-empty: Audio::makeVOCStream → _mixer->playStream()
	 *   4. Set _state.dialogueActive = true
	 *
	 * @param text     UTF-8 text (converted from CP437 source)
	 * @param alsFile  .ALS filename or empty string for no audio
	 */
	void showDialogue(const Common::String &text, const Common::String &alsFile = "");

	/**
	 * Queue a dialogue to play after the current one is dismissed.
	 * Used by Scene11Handler to chain Paco's greeting → Mariano's response.
	 */
	void queueDialogue(DialogueKey key);

	/**
	 * Transition to a new scene.
	 * Equivalent to _sal_de_la_habitacion() + _al_a_pantalla_k_eva() in HARE.EXE.
	 *
	 * @param destSceneAld  Destination .ALD filename (e.g. "9.ALD")
	 * @param destDoorId    Matching door ID in destination scene (PDN)
	 * @param destPos       Paco entry position (PPOS: 0=left, 1=right, 2=center)
	 */
	void transitionToScene(const Common::String &destSceneAld,
	                       int16 destDoorId, int16 destPos);

	/**
	 * Load and display the "auxilio" screen.
	 *
	 * STATUS: UNCONFIRMED PURPOSE — see AUXILIO_format_analysis_CORRECTED.md
	 * Section 4. String layout at 0xC60E confirmed (auxilio\0 98.alg\0 auxilio\0),
	 * but its role is one of two open, evidence-consistent hypotheses:
	 *   (a) dialogue/UI text ("Help!") tied to a 98.alg emergency screen, OR
	 *   (b) unrelated to gameplay dialogue — possibly the base filename HARE.EXE's
	 *       own save routine writes to (a backup slot alongside "gamesave").
	 * An earlier draft asserted (a) as settled fact; it is not. Do not wire
	 * this call site to any trigger condition until resolved by disassembly
	 * of the code around _graba_partida()/_carga_partida().
	 */
	void showAuxilioScreen();

	/**
	 * Save game confirmation (called from F5 / save menu).
	 * Plays 99.als (save audio, not in demo), shows kDlgSaveDone text.
	 */
	void confirmSave();

	/// Access game state (for handlers and renderer)
	GameState &getState() { return _state; }

private:
	// ── Initialization sequence ───────────────────────────────────────────

	/**
	 * Phase 1: Video init — equivalent to asigna_modo_video(0x13).
	 * In ScummVM: configures the 320×200 back-buffer surface.
	 * Original: INT 10h AH=00h AL=13h → 320×200 Mode 13h VGA.
	 */
	bool initVideo();

	/**
	 * Phase 2: Audio init — equivalent to INICIALIZADRV() + INICIALIZACMF().
	 * - INICIALIZADRV: loads ct-voice.drv (not needed — we use makeVOCStream)
	 * - INICIALIZACMF: initializes SBFMDRV.COM TSR (not needed — we use OPL emulation)
	 */
	bool initAudio();

	/**
	 * Phase 3: Load and display intro screens (97.alg / 98.alg).
	 * Original: _FundeDelNegro() fade-in after loading the title PCX.
	 * Skipped if assets are not present.
	 */
	void showIntroIfPresent();

	// ── Scene management ──────────────────────────────────────────────────

	/**
	 * Load a scene by ID string.
	 * Sequence:
	 *   1. _sal_de_la_habitacion(): release current ALG surface
	 *   2. Load new .ALG (PCX) → _background
	 *   3. Load and parse new .ALD (XOR-0xFF decode) → _currentAld
	 *   4. Switch music if MU$ changed (_musica_room vs _musica_antes)
	 *   5. Set up interaction dispatcher for new scene
	 *   6. Apply Paco entry position
	 */
	bool loadScene(const Common::String &sceneId, int16 entryPos = 2);

	/**
	 * Decode a .ALG PCX background into _background surface.
	 * Wraps Image::PCXDecoder. Handles the VGA palette (0-255 values directly
	 * usable by ScummVM — no >>2 shift needed for Graphics::Surface).
	 */
	bool loadBackground(const Common::String &algFile);

	// ── Game loop ─────────────────────────────────────────────────────────

	/**
	 * Main event processing loop — equivalent to the main() game loop in HARE.EXE.
	 * Polls events (no busy-wait — ScummVM event manager replaces INT 33h polling).
	 * Returns false when the engine should exit.
	 */
	bool processEvents();

	/**
	 * Handle a left mouse click at window coordinates.
	 * Converts to native 320×200 space, dismisses dialogue if active,
	 * or dispatches to InteractionDispatcher::dispatchClick().
	 */
	void handleMouseClick(int winX, int winY, bool leftButton);

	// ── Rendering pipeline ────────────────────────────────────────────────

	/**
	 * Render one complete frame to the screen.
	 * Layer order (matching _refresca_pantalla in HARE.EXE):
	 *   L0: fill _backbuf black           (_borra_pantalla)
	 *   L1: blit _background              (_pon_dibujos)
	 *   L2: blit disc-object sprites      (_pon_dibujos_colocao — needs 99.ALG)
	 *   L3: draw Paco sprite/placeholder  (_pon_hare)
	 *   L4: draw HUD / inventory          (_pon_dibujos_menu)
	 *   L5: draw dialogue box if active   (_hablar text portion)
	 *   FLIP: copyRectToScreen()          (_VUELCA_PANTALLA → A000:0000)
	 */
	void renderFrame();

	/// L1: Background blit (_pon_dibujos — 64000-byte copy to backbuf)
	void renderBackground();

	/// L2: Disc-object sprites (_pon_dibujos_colocao — requires 99.ALG)
	void renderDiscObjects();

	/// L3: Paco character (_pon_hare via _DIBUJA_BLOQUE_CON_MASK)
	void renderPaco();

	/// L4: HUD overlay (_pon_dibujos_menu — inventory icons from 99.ALG)
	void renderHud();

	/// L5: Dialogue box and text (_hablar + _centra_texto at y=155-199)
	void renderDialogue();

	/// FLIP: _VUELCA_PANTALLA — REP MOVSW to A000:0000 → copyRectToScreen()
	void pageFlip();

	// ── Paco movement (_lleva_al_hare + _aumenta_num_frame) ──────────────

	/// Advance Paco's position toward walk target by one frame.
	/// Replicates _lleva_al_hare(tx,ty) linear interpolation.
	void updatePacoMovement();

	// ── Audio playback ────────────────────────────────────────────────────

	/// Load and play a .ALS (VOC) file. Non-blocking (mixer thread).
	/// Replicates _ELIGEFICHFX(als_file) + _ctvd_output(voc_ptr).
	void playVoice(const Common::String &alsFile);

	/// Stop current voice playback (_ctvd_stop).
	void stopVoice();

	/// Load and start CMF music via OPL2 emulator.
	/// Replicates _ELIGEFICHFM(cmf_file) + ctvm_output.
	void playMusic(const Common::String &cmfFile);

	/// Stop music (_ctvm_stop equivalent).
	void stopMusic();

	/// Fade palette from black to normal (_FundeDelNegro equivalent).
	void fadeIn(int steps = 16);

	/// Fade palette to black (_FundeAlNegro equivalent).
	void fadeOut(int steps = 16);

	// ── Save/load helpers ─────────────────────────────────────────────────

	/// Apply _codifica() — XOR each byte with 0xFF — to save data.
	static void codificaBuffer(byte *buf, uint32 len);

	// ── Internal state ────────────────────────────────────────────────────

	const Paco1994GameDescription *_gameDesc;

	// Graphics
	Graphics::Surface *_backbuf;     ///< 320×200 back-buffer (farmalloc(64000L) equivalent)
	Graphics::Surface *_background;  ///< Current scene PCX decoded (far-heap ALG buffer)

	// Audio
	OPL::OPL         *_opl;         ///< OPL2 emulator for CMF music
	Audio::SoundHandle _voiceHandle; ///< Current VOC dialogue handle
	Audio::SoundHandle _musicHandle; ///< Current CMF music handle

	// Game state and logic
	GameState            _state;
	AldScene             _currentAld;    ///< Parsed ALD for current scene
	InteractionDispatcher _dispatcher;   ///< Routes clicks to scene handlers

	// Dialogue queue (for chained dialogue: greet → response)
	Common::Array<DialogueKey> _dlgQueue;

	// Scene loading
	bool _sceneLoaded;
	bool _pendingTransition;
	Common::String _pendingSceneId;
	int16          _pendingDoorId;
	int16          _pendingPos;
};

} // namespace Paco1994

#endif // PACO1994_ENGINE_H
