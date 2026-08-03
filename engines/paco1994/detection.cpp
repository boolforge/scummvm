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

#include "base/plugins.h"
#include "engines/advancedDetector.h"

#include "paco1994/detection.h"

/*
 * Detection entry for Paco El Hare vs Los Marcianos Siderales (1994).
 *
 * Verified hashes (original freeware distribution; see the companion wiki
 * article's "Detection" section for the full provenance and cross-checks):
 *   HARE.EXE  MD5 ed599780592c0302a571c88967e54b12  size 81179
 *
 * Minimum required files: HARE.EXE plus at least one matching .ALD/.ALG pair.
 * SCOVA.EXE (level editor), PLAY.EXE (IFF ANIM player), VPLAY.EXE (VOC test
 * player), and SBFMDRV.COM (FM TSR, superseded by ScummVM's software OPL
 * emulation) are development-time tools, not required at runtime.
 */

namespace Paco1994 {

static const PlainGameDescriptor paco1994Games[] = {
	{"pacohare1994", "Paco El Hare vs Los Marcianos Siderales"},
	{nullptr, nullptr}
};

static const ADGameDescription gameDescriptions[] = {
	{
		"pacohare1994", nullptr, AD_ENTRY1s("HARE.EXE", "ed599780592c0302a571c88967e54b12", 81179),
		Common::ES_ESP,
		Common::kPlatformDOS,
		ADGF_FREEWARE | ADGF_UNSTABLE,
		GUIO1(GUIO_NOSPEECH)
	},
	AD_TABLE_END_MARKER
};

static const DebugChannelDef debugFlagList[] = {
	{kDebugGraphics, "graphics", "PCX decode, page-flip, sprite compositing"},
	{kDebugAudio, "audio", "VOC/CMF loading and playback"},
	{kDebugALD, "ald", "ALD parse: hotspots, disc-objects, doors"},
	{kDebugInteract, "interact", "Hit-test and puzzle dispatch"},
	{kDebugState, "state", "Flag/inventory mutations, save/load"},
	DEBUG_CHANNEL_END
};

} // namespace Paco1994

class Paco1994MetaEngineDetection : public AdvancedMetaEngineDetection<ADGameDescription> {
public:
	Paco1994MetaEngineDetection() : AdvancedMetaEngineDetection(Paco1994::gameDescriptions, Paco1994::paco1994Games) {
	}

	const char *getEngineName() const override {
		return "Paco1994";
	}

	const char *getName() const override {
		return "paco1994";
	}

	const char *getOriginalCopyright() const override {
		return "Paco El Hare vs Los Marcianos Siderales (C) 1994 Alcachofa Soft S.L.";
	}

	const DebugChannelDef *getDebugChannels() const override {
		return Paco1994::debugFlagList;
	}
};

REGISTER_PLUGIN_STATIC(PACO1994_DETECTION, PLUGIN_TYPE_ENGINE_DETECTION, Paco1994MetaEngineDetection);
