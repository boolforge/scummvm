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

/*
 * Paco El Hare vs Los Marcianos Siderales (1994) — Alcachofa Soft S.L.
 * Reverse-engineered MS-DOS engine — detection layer
 *
 * NOTE ON INCOMPATIBILITY WITH engines/alcachofa:
 *   The existing engines/alcachofa engine is a 3D OpenGL engine for the
 *   2000s Windows "Mortadelo y Filemón" series (V1.0–V3.1). It uses:
 *     - OpenGL/TinyGL rendering, 3D world/camera system
 *     - Full bytecode script VM (ScriptOp enum, 30+ opcodes)
 *     - .nkr/.BIN binary resource files
 *     - Windows platform only
 *   The 1994 DOS game is architecturally INCOMPATIBLE:
 *     - 2D Mode 13h 320×200 VGA rendering
 *     - NO script VM — all logic hardcoded in HARE.EXE
 *     - .ALD/.ALG/.ALS DOS file formats
 *     - MS-DOS Real Mode x86
 *   A completely separate engine is required.
 *
 * SCUMMVM APIs REUSED:
 *   - Image::PCXDecoder (image/pcx.h) — .ALG PCX ZSoft v3.0 backgrounds
 *   - Audio::makeVOCStream (audio/decoders/voc.h) — .ALS Creative VOC 1.10
 *   - OPL::OPL (audio/fmopl.h) — AULD.CMF/STARFM.CMF OPL2 FM music
 *   - Common::File, Common::String, Common::SaveFileManager
 *   - Graphics::Surface for 2D back-buffer
 */

#ifndef PACO1994_DETECTION_H
#define PACO1994_DETECTION_H

#include "engines/advancedDetector.h"

namespace Paco1994 {

enum Paco1994DebugChannels {
	kDebugGraphics   = 1 << 0,  ///< PCX decode, page-flip (_VUELCA_PANTALLA)
	kDebugAudio      = 1 << 1,  ///< VOC/CMF loading and playback
	kDebugALD        = 1 << 2,  ///< ALD parse: XOR decode, hotspot data
	kDebugInteract   = 1 << 3,  ///< Hotspot hit-test and puzzle dispatch
	kDebugState      = 1 << 4,  ///< Flag/inventory mutations (_flags[], _objetos_que_tengo)
};

/**
 * The 1994 game only has one "version" — the original freeware DOS demo.
 * Reserved for future discoveries (e.g. earlier alpha builds).
 */
enum class Paco1994Version {
	kV1994Original = 0,  ///< HARE.EXE MD5: ed599780592c0302a571c88967e54b12
};

/**
 * Game description record, bundled with our version tag.
 * Follows the same pattern as AlcachofaGameDescription in engines/alcachofa.
 */
struct Paco1994GameDescription {
	AD_GAME_DESCRIPTION_HELPERS(desc);
	ADGameDescription    desc;
	Paco1994Version      version;
};

extern const PlainGameDescriptor      paco1994Games[];
extern const Paco1994GameDescription  paco1994GameDescriptions[];

} // namespace Paco1994

// ---------------------------------------------------------------------------
// MetaEngine detection class (registered as a static plugin)
// ---------------------------------------------------------------------------
class Paco1994MetaEngineDetection : public AdvancedMetaEngineDetection<Paco1994::Paco1994GameDescription> {
public:
	Paco1994MetaEngineDetection();

	const char *getName() const override { return "paco1994"; }
	const char *getEngineName() const override { return "Paco El Hare Engine (1994)"; }
	const char *getOriginalCopyright() const override {
		return "Paco El Hare vs Los Marcianos Siderales (C) 1994 Alcachofa Soft S.L.";
	}

	static const DebugChannelDef debugFlagList[];
};

#endif // PACO1994_DETECTION_H
