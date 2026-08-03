/* ScummVM - Graphic Adventure Engine
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "paco1994/paco1994.h"
#include "paco1994/ald_parser.h"
#include "paco1994/interaction.h"

#include "common/config-manager.h"
#include "common/debug.h"
#include "common/events.h"
#include "common/file.h"
#include "common/system.h"
#include "common/textconsole.h"
#include "common/translation.h"

#include "audio/audiostream.h"
#include "audio/decoders/voc.h"
#include "audio/fmopl.h"
#include "audio/mixer.h"

#include "graphics/surface.h"
#include "graphics/palette.h"
#include "image/pcx.h"

namespace Paco1994 {

// ── Constructor / Destructor ──────────────────────────────────────────────────

Paco1994Engine::Paco1994Engine(OSystem *syst, const Paco1994GameDescription *gameDesc)
	: Engine(syst),
	  _gameDesc(gameDesc),
	  _backbuf(nullptr),
	  _background(nullptr),
	  _opl(nullptr),
	  _sceneLoaded(false),
	  _pendingTransition(false),
	  _pendingDoorId(0),
	  _pendingPos(2) {

	// Register debug channels from detection.h
	DebugMan.addDebugChannel(kDebugGraphics,  "graphics",  "PCX decode, page-flip");
	DebugMan.addDebugChannel(kDebugAudio,     "audio",     "VOC/CMF loading");
	DebugMan.addDebugChannel(kDebugALD,       "ald",       "ALD parse and hotspot data");
	DebugMan.addDebugChannel(kDebugInteract,  "interact",  "Hit-test and puzzle dispatch");
	DebugMan.addDebugChannel(kDebugState,     "state",     "Flag/inventory mutations");
}

Paco1994Engine::~Paco1994Engine() {
	// Free graphics surfaces (equivalent to farfree() calls in _salir_al_dos)
	if (_backbuf) {
		_backbuf->free();
		delete _backbuf;
	}
	if (_background) {
		_background->free();
		delete _background;
	}
	// OPL cleanup (_ctvd_terminate equivalent)
	if (_opl) {
		_opl->stop();
		delete _opl;
	}
	stopVoice();
	stopMusic();
}

// ── Engine::run() — main entry point ─────────────────────────────────────────

/**
 * Main engine loop. Called by ScummVM after the engine is instantiated.
 *
 * Original HARE.EXE sequence:
 *   1. inicializa_raton_grafico()    → INT 33h AX=0
 *   2. asigna_modo_video(0x13)       → INT 10h AX=0013h
 *   3. INICIALIZADRV()               → load ct-voice.drv into far heap
 *   4. INICIALIZACMF()               → connect to SBFMDRV.COM TSR
 *   5. setvgapalette256(black)       → suppress palette flash
 *   6. Load intro screens (97/98.alg)
 *   7. Main game loop
 *   8. salir_al_dos()                → INT 21h AH=4Ch
 */
Common::Error Paco1994Engine::run() {
	debugC(1, kDebugGraphics, "Paco1994Engine::run() starting");

	// ── Phase 1: Video init ───────────────────────────────────────────────
	if (!initVideo())
		return Common::kUnknownError;

	// ── Phase 2: Audio init ───────────────────────────────────────────────
	if (!initAudio())
		warning("Audio init failed — running silent");

	// ── Phase 3: Intro screens (97.alg / 98.alg) ─────────────────────────
	showIntroIfPresent();

	// ── Phase 4: Load initial scene ───────────────────────────────────────
	// Start at scene "1" (P$="1" in 1.ALD), Paco enters at center (PPOS=2)
	if (!loadScene("1", 2)) {
		warning("Failed to load starting scene '1' — check that 1.ALD and 1.ALG are present");
		return Common::kNoGameDataFoundError;
	}

	// ── Phase 5: Main game loop ───────────────────────────────────────────
	// CONFIRMED via direct disassembly (reko-decomp.txt:2758-2815, the loop
	// body of _escoba -- verified against real bytes, not inferred):
	//   do {
	//     _desactiva_raton();                 // hide cursor before redraw
	//     if (slotMachineMode) _mueve_objeto(); else _refresca_pantalla();
	//     _VUELCA_PANTALLA(0,0,0,0, 320,199, backbufSeg, backbufOff);
	//     _activa_raton();                     // restore cursor
	//     int86(0x33, ...);                    // poll mouse (buttons + xy)
	//     if (leftClick && !slotMachineMode) _comprueba1(x, y);
	//     if (leftClick &&  slotMachineMode) _comprueba2(x, y);
	//     key = _bioskey(...);
	//     if (key == 0x3B) _para_grabar();      // F1 = save
	//     if (key == 0x3C) _para_cargar();      // F2 = load
	//   } while (!quitFlag);
	// ScummVM: event-driven via g_system->getEventManager(); the dual
	// _comprueba1/_comprueba2 dispatch (normal vs. slot-machine mode) and
	// the F1/F2 save/load bindings are not yet wired below -- TODO.
	while (!shouldQuit()) {
		// Handle pending scene transition (deferred to avoid mid-frame switch)
		if (_pendingTransition) {
			_pendingTransition = false;
			loadScene(_pendingSceneId, _pendingPos);
		}

		// Process queued dialogue chain (Scene11Handler chained responses)
		if (!_dlgQueue.empty() && !_state.dialogueActive) {
			DialogueKey key = _dlgQueue.front();
			_dlgQueue.remove_at(0);
			// Fire queued dialogue via interaction system
			const DialogueEntry &e = kDialogues[key];
			showDialogue(e.textEN, e.alsFile ? e.alsFile : "");
		}

		// Process OS events (_espera_pulsar / keyboard poll equivalent)
		if (!processEvents())
			break;

		// Update Paco movement (_lleva_al_hare + _aumenta_num_frame)
		updatePacoMovement();

		// Render frame (_refresca_pantalla + _VUELCA_PANTALLA)
		renderFrame();

		// Frame delay: original had no cap (CPU-speed dependent).
		// 30ms ≈ 33fps soft cap — adjust for authenticity if needed.
		_system->delayMillis(30);
	}

	return Common::kNoError;
}

// ── Engine features ───────────────────────────────────────────────────────────

bool Paco1994Engine::hasFeature(EngineFeature f) const {
	return
		(f == kSupportsReturnToLauncher) ||
		(f == kSupportsSavingDuringRuntime) ||
		(f == kSupportsLoadingDuringRuntime);
}

bool Paco1994Engine::canLoadGameStateCurrently(Common::U32String *) {
	// Allow load any time dialogue is not active
	return !_state.dialogueActive;
}

bool Paco1994Engine::canSaveGameStateCurrently(Common::U32String *) {
	return !_state.dialogueActive;
}

// ── Save / Load ───────────────────────────────────────────────────────────────

/**
 * Save game state to stream.
 * Replicates _graba_partida() + _codifica() from HARE.EXE.
 * Original: writes _flags[] + _objetos_que_tengo[] as raw binary, then XOR-0xFF.
 */
Common::Error Paco1994Engine::saveGameStream(Common::WriteStream *stream, bool isAutosave) {
	Common::Serializer s(nullptr, stream);
	_state.synchronize(s);
	debugC(1, kDebugState, "Game saved");
	return Common::kNoError;
}

/**
 * Load game state from stream.
 * Replicates _carga_partida() + _codifica() from HARE.EXE.
 */
Common::Error Paco1994Engine::loadGameStream(Common::SeekableReadStream *stream) {
	Common::Serializer s(stream, nullptr);
	_state.synchronize(s);
	// Reload current scene after state restore
	loadScene(_state.currentScene, 2);
	debugC(1, kDebugState, "Game loaded, scene='%s'", _state.currentScene.c_str());
	return Common::kNoError;
}

void Paco1994Engine::codificaBuffer(byte *buf, uint32 len) {
	// _codifica(char *buf, int len): for(i=0;i<len;i++) buf[i]^=0xFF;
	for (uint32 i = 0; i < len; i++)
		buf[i] ^= 0xFF;
}

// ── Initialization ────────────────────────────────────────────────────────────

bool Paco1994Engine::initVideo() {
	// Allocate 320×200 back-buffer (equivalent to farmalloc(64000L) in HARE.EXE)
	// Original: char far *backbuf = farmalloc(64000L);
	_backbuf = new Graphics::Surface();
	_backbuf->create(kNativeW, kNativeH, Graphics::PixelFormat::createFormatCLUT8());
	if (!_backbuf->getPixels()) {
		warning("initVideo: failed to allocate 320×200 back-buffer");
		return false;
	}

	// Set ScummVM output resolution
	initGraphics(kNativeW, kNativeH);
	debugC(1, kDebugGraphics, "Video: 320×200 back-buffer ready");
	return true;
}

bool Paco1994Engine::initAudio() {
	// Create OPL2 emulator for CMF music (replaces SBFMDRV.COM TSR + CTVM API)
	_opl = OPL::Config::create(OPL::Config::kOpl2);
	if (!_opl) {
		warning("initAudio: OPL2 emulator unavailable — CMF music disabled");
		// VOC voice playback still works via Audio::makeVOCStream
	} else {
		_opl->init();
		debugC(1, kDebugAudio, "Audio: OPL2 emulator ready for CMF music");
	}
	return true;
}

void Paco1994Engine::showIntroIfPresent() {
	// Load 97.alg (title/intro screen) if present, fade in, wait for click
	Common::File f;
	if (f.open("97.alg") || f.open("97.ALG")) {
		debugC(1, kDebugGraphics, "Loading intro screen 97.alg");
		f.close();
		if (loadBackground("97.alg")) {
			fadeIn(16);
			// Wait for click (equivalent to espera_pulsar())
			Common::Event ev;
			bool clicked = false;
			while (!clicked && !shouldQuit()) {
				while (_eventMan->pollEvent(ev)) {
					if (ev.type == Common::EVENT_LBUTTONDOWN) clicked = true;
					if (ev.type == Common::EVENT_KEYDOWN &&
					    ev.kbd.keycode == Common::KEYCODE_ESCAPE) clicked = true;
				}
				renderBackground();
				pageFlip();
				_system->delayMillis(16);
			}
			fadeOut(16);
		}
	}
}

// ── Scene management ──────────────────────────────────────────────────────────

bool Paco1994Engine::loadScene(const Common::String &sceneId, int16 entryPos) {
	debugC(1, kDebugALD, "loadScene: '%s' entryPos=%d", sceneId.c_str(), entryPos);

	// Determine filenames from scene ID
	Common::String aldFile = sceneId + ".ALD";
	Common::String algFile = sceneId + ".ALG";

	// ── Release previous background (_sal_de_la_habitacion: farfree) ─────
	if (_background) {
		_background->free();
		delete _background;
		_background = nullptr;
	}

	// ── Load .ALG background (PCX ZSoft v3.0) ────────────────────────────
	if (!loadBackground(algFile)) {
		// Try lowercase
		algFile.toLowercase();
		if (!loadBackground(algFile)) {
			warning("loadScene: ALG not found for scene '%s'", sceneId.c_str());
			// Create placeholder dark background
			_background = new Graphics::Surface();
			_background->create(kNativeW, kNativeH, Graphics::PixelFormat::createFormatCLUT8());
			_background->fillRect(Common::Rect(kNativeW, kNativeH), 0);
		}
	}

	// ── Load and parse .ALD scene data (XOR-0xFF decode) ─────────────────
	{
		Common::File aldStream;
		if (!aldStream.open(aldFile)) {
			aldFile.toLowercase();
			if (!aldStream.open(aldFile)) {
				warning("loadScene: ALD not found for scene '%s'", sceneId.c_str());
				return false;
			}
		}
		AldParser parser;
		if (!parser.parseScene(aldStream, _currentAld)) {
			warning("loadScene: ALD parse failed for scene '%s'", sceneId.c_str());
			return false;
		}
	}

	// ── Music transition (_musica_antes vs _musica_room) ──────────────────
	if (_currentAld.musicFile != _state.currentMusic) {
		_state.prevMusic    = _state.currentMusic;
		_state.currentMusic = _currentAld.musicFile;
		if (!_currentAld.musicFile.empty())
			playMusic(_currentAld.musicFile);
	}

	// ── Update game state ─────────────────────────────────────────────────
	_state.currentScene  = sceneId;
	_state.dialogueActive = false;
	_state.clearDialogue();
	_dlgQueue.clear();

	// Apply PPOS entry position (_sitio: 0=left x≈20, 1=right x≈300, 2=center x=160)
	_state.applyEntryPos(entryPos);

	// ── Set up interaction dispatcher for new scene ───────────────────────
	_dispatcher.setScene(sceneId);
	_sceneLoaded = true;

	debugC(1, kDebugALD, "Scene '%s' loaded: %u objects, %u disc-objs, music='%s'",
	       sceneId.c_str(),
	       (uint)_currentAld.objects.size(),
	       (uint)_currentAld.discObjects.size(),
	       _currentAld.musicFile.c_str());

	return true;
}

bool Paco1994Engine::loadBackground(const Common::String &algFile) {
	Common::File f;
	if (!f.open(algFile))
		return false;

	// Use ScummVM's PCX decoder (image/pcx.h — Image::PCXDecoder)
	// Handles ZSoft PCX v3.0 with VGA palette natively.
	Image::PCXDecoder pcxDec;
	if (!pcxDec.loadStream(f)) {
		warning("loadBackground: PCX decode failed for '%s'", algFile.c_str());
		return false;
	}

	// Copy decoded surface to our background (we own it)
	const Graphics::Surface *decoded = pcxDec.getSurface();
	if (!decoded) return false;

	_background = new Graphics::Surface();
	_background->copyFrom(*decoded);

	// Apply VGA palette from PCX to the system palette
	// PCX stores values 0-255; ScummVM Palette accepts 0-255 (no >>2 shift here)
	const byte *pal = pcxDec.getPalette();
	if (pal)
		_system->getPaletteManager()->setPalette(pal, 0, 256);

	debugC(1, kDebugGraphics, "Background loaded: '%s' %dx%d",
	       algFile.c_str(), decoded->w, decoded->h);
	return true;
}

// ── Public API ────────────────────────────────────────────────────────────────

void Paco1994Engine::showDialogue(const Common::String &text, const Common::String &alsFile) {
	_state.dialogueText   = text;
	_state.dialogueAudio  = alsFile;
	_state.dialogueActive = true;

	if (!alsFile.empty())
		playVoice(alsFile);

	debugC(1, kDebugInteract, "Dialogue: '%s' [%s]",
	       text.c_str(), alsFile.empty() ? "no audio" : alsFile.c_str());
}

void Paco1994Engine::queueDialogue(DialogueKey key) {
	_dlgQueue.push_back(key);
}

void Paco1994Engine::transitionToScene(const Common::String &destSceneAld,
                                       int16 destDoorId, int16 destPos) {
	// Extract scene ID from filename "10.ALD" → "10"
	Common::String destId = AldParser::sceneIdFromFilename(destSceneAld);

	debugC(1, kDebugInteract, "Transition: → scene '%s' door=%d pos=%d",
	       destId.c_str(), destDoorId, destPos);

	// Defer actual load to top of game loop to avoid mid-frame state corruption
	_pendingTransition = true;
	_pendingSceneId    = destId;
	_pendingDoorId     = destDoorId;
	_pendingPos        = destPos;

	// Clear any active dialogue
	stopVoice();
	_state.clearDialogue();
}

void Paco1994Engine::showAuxilioScreen() {
	// STATUS: purpose unconfirmed — see AUXILIO_format_analysis_CORRECTED.md §4.
	// String layout at 0xC60E is confirmed (auxilio\0 98.alg\0 auxilio\0), but
	// whether this is a gameplay "help" screen or something unrelated to the
	// dialogue table entirely is NOT established. Not called from anywhere in
	// this engine yet — kept as a documented stub pending disassembly of the
	// code path around this string table entry.
	debugC(1, kDebugInteract, "AUXILIO screen requested (purpose unconfirmed, see doc)");

	fadeOut(8);
	if (loadBackground("98.alg") || loadBackground("98.ALG")) {
		fadeIn(8);
	}
	showDialogue(_("¡auxilio!"), "");
}

void Paco1994Engine::confirmSave() {
	// Save confirmation dialogue: "Acabas de grabar la partida, tio."
	// NOTE: the actual on-disk save write must NOT apply codifica()/XOR —
	// see GameState::synchronize() in game_state.h for the corrected,
	// verified-plain-text save format (GAMESAVE/AUXILIO/AUXILIAR ground truth).
	// 99.als (save confirmation sound) is referenced in HARE.EXE at 0xC416
	// but is not among the demo's available audio assets.
	showDialogue(kDialogues[kDlgSaveDone].textEN,
	             kDialogues[kDlgSaveDone].alsFile ? kDialogues[kDlgSaveDone].alsFile : "");
}

// ── Event processing ──────────────────────────────────────────────────────────

bool Paco1994Engine::processEvents() {
	Common::Event ev;
	while (_eventMan->pollEvent(ev)) {
		switch (ev.type) {
		case Common::EVENT_QUIT:
		case Common::EVENT_RETURN_TO_LAUNCHER:
			return false;

		case Common::EVENT_KEYDOWN:
			if (ev.kbd.keycode == Common::KEYCODE_ESCAPE)
				return false;
			if (ev.kbd.keycode == Common::KEYCODE_F5 &&
			    canSaveGameStateCurrently()) {
				// Quick save (_graba_partida equivalent)
				saveGameState(0, _("Quick Save"));
				confirmSave();
			}
			if (ev.kbd.keycode == Common::KEYCODE_F9 &&
			    canLoadGameStateCurrently()) {
				loadGameState(0);
			}
			break;

		case Common::EVENT_LBUTTONDOWN:
			handleMouseClick(ev.mouse.x, ev.mouse.y, true);
			break;

		default:
			break;
		}
	}
	return true;
}

void Paco1994Engine::handleMouseClick(int winX, int winY, bool leftButton) {
	// Convert window coords to native 320×200 space
	// ScummVM scales the display; mouse coords are in logical (320×200) space
	// when using initGraphics(320, 200) — no division needed.
	int16 nx = (int16)CLIP(winX, 0, kNativeW - 1);
	int16 ny = (int16)CLIP(winY, 0, kNativeH - 1);

	// Dismiss active dialogue on click (_espera_pulsar / espera_soltar)
	if (_state.dialogueActive) {
		stopVoice();
		_state.clearDialogue();
		// If there's a pending transition (guard passage), execute it now
		return;
	}

	if (!_sceneLoaded) return;

	// Dispatch click via interaction system (_comprueba(x,y))
	bool hotspotHit = _dispatcher.dispatchClick(
		nx, ny, _currentAld, _state, *this);

	if (!hotspotHit) {
		// No hotspot: set Paco's walk target (_lleva_al_hare)
		// Clamp to walkable zone (y < kWalkMaxY = 148)
		_state.targetX    = nx;
		_state.targetY    = (int16)MIN((int)ny, kWalkMaxY);
		_state.hareMoving = true;
	}
}

// ── Rendering pipeline ────────────────────────────────────────────────────────

void Paco1994Engine::renderFrame() {
	if (!_backbuf) return;

	// L0: Clear back-buffer (_borra_pantalla: memset to palette index 0)
	_backbuf->fillRect(Common::Rect(kNativeW, kNativeH), 0);

	// L1: Background (_pon_dibujos: 64000-byte copy)
	renderBackground();

	// L2: Disc-object sprites (_pon_dibujos_colocao — needs 99.ALG)
	renderDiscObjects();

	// L3: Paco character (_pon_hare via DIBUJA_BLOQUE_CON_MASK)
	renderPaco();

	// L4: HUD / inventory (_pon_dibujos_menu)
	renderHud();

	// L5: Dialogue box (_hablar text + black rect at y=155-199)
	if (_state.dialogueActive)
		renderDialogue();

	// FLIP: _VUELCA_PANTALLA → copyRectToScreen (A000:0000 equivalent)
	pageFlip();
}

void Paco1994Engine::renderBackground() {
	if (!_background || !_backbuf) return;
	// _pon_dibujos(): memcpy(backbuf, alg_buf, 64000)
	// In ScummVM: blit from decoded PCX surface to back-buffer
	_backbuf->copyRectToSurface(*_background,
	    0, 0,
	    Common::Rect(0, 0, kNativeW, kNativeH));
}

void Paco1994Engine::renderDiscObjects() {
	// _pon_dibujos_colocao(): for each disc_obj, DIBUJA_BLOQUE_CON_MASK
	// Requires 99.ALG sprite sheet. If unavailable: skip (assets missing in demo).
	// When 99.ALG is available, implement:
	//   for (disc_obj : _currentAld.discObjects)
	//     blit 99alg_surface[sheet_rect] → backbuf at (place.x, place.y)
	//     using masked blit (mask row at sheet_y + AldDiscObject::kMaskYOff)
	//     sprite dimensions: kWidth=39, kTrueHeight=24 (NOT sheet_rect.height()!)
	debugC(5, kDebugGraphics, "renderDiscObjects: %u objects (99.ALG required)",
	       (uint)_currentAld.discObjects.size());
}

void Paco1994Engine::renderPaco() {
	// _pon_hare(): DIBUJA_BLOQUE_CON_MASK(dir_hare_dch/izq, mask, hare_x, hare_y, w, h)
	// TODO: load Paco sprite sheets (dir_hare_dch/dir_hare_izq — location unknown)
	// Placeholder: draw a simple colored rectangle at Paco's position
	if (!_backbuf) return;

	int16 hx = _state.hareX;
	int16 hy = _state.hareY;

	// 16×24 placeholder (approximate Paco sprite dimensions)
	Common::Rect pacoRect(hx - 8, hy - 20, hx + 8, hy + 4);
	pacoRect.clip(Common::Rect(kNativeW, kNativeH));

	if (!pacoRect.isEmpty()) {
		// Draw placeholder body (palette index 12 = typically red in VGA)
		_backbuf->fillRect(pacoRect, 12);
	}
}

void Paco1994Engine::renderHud() {
	// _pon_dibujos_menu(): renders inventory icons
	// Requires 99.ALG (sprite sheet with inventory object sprites).
	// TODO: implement when 99.ALG available
}

void Paco1994Engine::renderDialogue() {
	if (!_backbuf || _state.dialogueText.empty()) return;

	// Draw black dialogue box at y=155-199 (kDlgBoxY=155, kDlgBoxH=45)
	// Original: LINE (0,155)-(319,199), 0, BF  [filled black rectangle]
	Common::Rect dlgRect(0, kDlgBoxY, kNativeW, kNativeH);
	_backbuf->fillRect(dlgRect, 0);  // palette index 0 = black

	// TODO: render centered text using ScummVM's Font system
	// _centra_texto(160, text): x = (320 - text_width) / 2, y = 160
	// For now, dialogue is shown via ScummVM's OSD/subtitles system
}

void Paco1994Engine::pageFlip() {
	// _VUELCA_PANTALLA(): REP MOVSW from backbuf to A000:0000
	// ScummVM equivalent: copyRectToScreen
	if (!_backbuf) return;
	_system->copyRectToScreen(
	    _backbuf->getPixels(),
	    _backbuf->pitch,
	    0, 0,
	    kNativeW, kNativeH);
	_system->updateScreen();
}

// ── Paco movement ─────────────────────────────────────────────────────────────

void Paco1994Engine::updatePacoMovement() {
	if (!_state.hareMoving) return;

	int16 dx = _state.targetX - _state.hareX;
	int16 dy = _state.targetY - _state.hareY;
	int32 dist2 = (int32)dx * dx + (int32)dy * dy;

	const int kStep = kMoveStep; // 3 pixels/frame from game_state.h

	if (dist2 <= kStep * kStep) {
		// Arrived
		_state.hareX      = _state.targetX;
		_state.hareY      = _state.targetY;
		_state.hareMoving = false;
		_state.frameIndex = 0;
	} else {
		// Linear step toward target (_lleva_al_hare)
		float dist = (float)Common::sqrt((float)dist2);
		_state.hareX += (int16)((float)dx / dist * kStep);
		_state.hareY += (int16)((float)dy / dist * kStep);
		_state.hareFacing = (dx < 0) ? 0 : 1; // _sentido_hare

		// Advance walk animation (_aumenta_num_frame)
		_state.frameTimer++;
		if (_state.frameTimer >= 6) {
			_state.frameTimer = 0;
			_state.frameIndex = (_state.frameIndex + 1) % kWalkFrameCount;
		}
	}
}

// ── Audio ─────────────────────────────────────────────────────────────────────

void Paco1994Engine::playVoice(const Common::String &alsFile) {
	// _ELIGEFICHFX(als_file) + _ctvd_output(voc_far_ptr)
	// ScummVM: Audio::makeVOCStream → _mixer->playStream

	Common::File *f = new Common::File();
	if (!f->open(alsFile)) {
		// Try case-insensitive fallback
		Common::String lower(alsFile);
		lower.toLowercase();
		if (!f->open(lower)) {
			delete f;
			debugC(1, kDebugAudio, "VOC not found: '%s'", alsFile.c_str());
			return;
		}
	}

	// Audio::makeVOCStream: parses Creative Labs VOC v1.10
	// PCM 8-bit unsigned, mono, 21739 Hz (divisor D=210)
	// Audio::FLAG_UNSIGNED: converts u8 to s16 internally
	Audio::SeekableAudioStream *stream =
	    Audio::makeVOCStream(f, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
	if (!stream) {
		delete f;
		warning("playVoice: makeVOCStream failed for '%s'", alsFile.c_str());
		return;
	}

	// Stop any current voice (_ctvd_stop)
	stopVoice();

	// Non-blocking playback (DMA-equivalent: mixer thread handles audio)
	_mixer->playStream(Audio::Mixer::kSpeechSoundType,
	                   &_voiceHandle, stream);

	debugC(1, kDebugAudio, "Playing VOC: '%s'", alsFile.c_str());
}

void Paco1994Engine::stopVoice() {
	if (_mixer->isSoundHandleActive(_voiceHandle))
		_mixer->stopHandle(_voiceHandle);
}

void Paco1994Engine::playMusic(const Common::String &cmfFile) {
	// _ELIGEFICHFM(cmf_file) → CTVM API → SBFMDRV.COM → OPL2 registers
	// ScummVM: custom CMF sequencer feeding OPL2 emulator
	// TODO: implement CMF parser for AULD.CMF and STARFM.CMF
	// CMF format: "CTMF" magic, instrument block, sequence block (OPL register data)
	debugC(1, kDebugAudio, "CMF music requested: '%s' (TODO: implement CMF parser)",
	       cmfFile.c_str());
}

void Paco1994Engine::stopMusic() {
	if (_mixer->isSoundHandleActive(_musicHandle))
		_mixer->stopHandle(_musicHandle);
}

void Paco1994Engine::fadeIn(int steps) {
	// _FundeDelNegro(): increments palette channels from 0 to target over 'steps' frames
	// ScummVM: animate palette fade using getPaletteManager
	// TODO: implement proper fade using saved palette + step interpolation
	debugC(1, kDebugGraphics, "fadeIn(%d) — TODO: palette interpolation", steps);
}

void Paco1994Engine::fadeOut(int steps) {
	// _FundeAlNegro(): decrements all palette channels to 0
	debugC(1, kDebugGraphics, "fadeOut(%d) — TODO: palette interpolation", steps);
}

} // namespace Paco1994
