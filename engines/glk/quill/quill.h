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

#ifndef GLK_QUILL_H
#define GLK_QUILL_H

#include "glk/glk_api.h"
#include "glk/quill/database.h"
#include "glk/quill/vm.h"

namespace Glk {
namespace Quill {

/**
 * Atari 800/XL/XE The Quill / AdventureWriter interpreter, hosted inside
 * ScummVM's Glk engine alongside the other supported IF systems.
 */
class Quill : public GlkAPI, public QuillIOHandler {
private:
	QuillDatabase _database;
	QuillVM *_vm;
	TextBufferWindow *_window;

	void initializeWindow();

protected:
	// GlkAPI
	void runGame() override;

	/**
	 * Restore the state of the game with a saved game file
	 */
	Common::Error loadGameChunks(QuetzalReader &quetzal) override;

	/**
	 * Saves the game
	 */
	Common::Error saveGameChunks(QuetzalWriter &quetzal) override;

public:
	Quill(OSystem *syst, const GlkGameDescription &gameDesc);
	~Quill() override;

	bool hasFeature(EngineFeature f) const override {
		return
			(f == kSupportsLoadingDuringRuntime) ||
			(f == kSupportsSavingDuringRuntime) ||
			(f == kSupportsReturnToLauncher);
	}

	/**
	 * Returns the running interpreter type
	 */
	InterpreterType getInterpreterType() const override {
		return INTERPRETER_QUILL;
	}

	// QuillIOHandler
	void print(const Common::String &text) override;
	void newLine() override;
	Common::String readLine() override;
	void waitKey() override;
	void clearScreen() override;
	void setColors(byte textLuminance, byte background, byte border) override;
	void sound(byte voice, byte pitch, byte distortion, byte volume) override;
	bool doSaveGame() override;
	bool doLoadGame() override;
	bool shouldQuit() const override {
		return GlkAPI::shouldQuit();
	}
};

} // End of namespace Quill
} // End of namespace Glk

#endif
