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

#include "glk/quill/quill.h"
#include "glk/quetzal.h"
#include "common/file.h"
#include "common/config-manager.h"

namespace Glk {
namespace Quill {

Quill::Quill(OSystem *syst, const GlkGameDescription &gameDesc) :
		GlkAPI(syst, gameDesc), _vm(nullptr), _window(nullptr) {
}

Quill::~Quill() {
	delete _vm;
}

void Quill::initializeWindow() {
	_window = (TextBufferWindow *)glk_window_open(nullptr, 0, 0, wintype_TextBuffer, 1);
	glk_set_window(_window);
}

void Quill::runGame() {
	Common::File gameFile;
	if (!gameFile.open(getFilename())) {
		GUIErrorMessage(_("Could not open game file"));
		return;
	}

	Common::String error;
	if (!_database.load(&gameFile, &error)) {
		GUIErrorMessage(Common::String::format(_("Unable to load game database: %s"), error.c_str()));
		return;
	}
	gameFile.close();

	initializeWindow();

	_vm = new QuillVM(_database, *this);

	if (ConfMan.hasKey("save_slot")) {
		// A specific slot was requested at launch (e.g. "Load latest save").
		// The game's own LOAD condact still works normally afterwards.
		loadGameState(ConfMan.getInt("save_slot"));
	}

	_vm->run();

	glk_window_close(_window);
}

void Quill::print(const Common::String &text) {
	if (!_window)
		return;

	Common::WriteStream *stream = glk_window_get_stream(_window);
	Common::String plain;
	bool inverse = false;

	for (uint i = 0; i < text.size(); ++i) {
		char c = text[i];
		if (c == QuillDatabase::INVERSE_ON || c == QuillDatabase::INVERSE_OFF) {
			if (!plain.empty()) {
				glk_put_string_stream(stream, plain.c_str());
				plain.clear();
			}
			bool wantInverse = (c == QuillDatabase::INVERSE_ON);
			if (wantInverse != inverse) {
				glk_set_style_stream(stream, wantInverse ? style_Emphasized : style_Normal);
				inverse = wantInverse;
			}
		} else {
			plain += c;
		}
	}

	if (!plain.empty())
		glk_put_string_stream(stream, plain.c_str());
	if (inverse)
		glk_set_style_stream(stream, style_Normal);
}

void Quill::newLine() {
	if (_window)
		glk_put_string_stream(glk_window_get_stream(_window), "\n");
}

Common::String Quill::readLine() {
	if (!_window)
		return Common::String();

	char buffer[256];
	event_t ev;
	glk_request_line_event(_window, buffer, sizeof(buffer) - 1, 0);

	for (;;) {
		glk_select(&ev);
		if (ev.type == evtype_Quit) {
			glk_cancel_line_event(_window, &ev);
			return Common::String();
		} else if (ev.type == evtype_LineInput) {
			break;
		}
	}

	buffer[ev.val1] = '\0';
	return Common::String(buffer);
}

void Quill::waitKey() {
	if (!_window)
		return;

	event_t ev;
	glk_request_char_event(_window);

	for (;;) {
		glk_select(&ev);
		if (ev.type == evtype_Quit) {
			glk_cancel_char_event(_window);
			return;
		} else if (ev.type == evtype_CharInput) {
			break;
		}
	}
}

void Quill::clearScreen() {
	if (_window)
		glk_window_clear(_window);
}

void Quill::setColors(byte textLuminance, byte background, byte border) {
	// Atari GTIA hue/luminance registers have no equivalent in a windowed
	// Glk text view - intentionally not applied. Kept as a hook in case a
	// future Glk backend adds window background colour support.
}

void Quill::sound(byte voice, byte pitch, byte distortion, byte volume) {
	// POKEY sound synthesis isn't emulated; SOUND is accepted and ignored.
}

bool Quill::doSaveGame() {
	return saveGame() == Common::kNoError;
}

bool Quill::doLoadGame() {
	return loadGame() == Common::kNoError;
}

Common::Error Quill::saveGameChunks(QuetzalWriter &quetzal) {
	if (!_vm)
		return Common::kWritingFailed;

	Common::WriteStream &out = quetzal.add(MKTAG('Q', 'u', 'i', 'l'));
	out.write(_vm->flagsPtr(), QuillVM::FLAG_COUNT);

	const Common::Array<byte> &objLoc = _vm->objLoc();
	out.writeUint32LE(objLoc.size());
	if (!objLoc.empty())
		out.write(&objLoc[0], objLoc.size());

	return Common::kNoError;
}

Common::Error Quill::loadGameChunks(QuetzalReader &quetzal) {
	if (!_vm)
		return Common::kReadingFailed;

	for (QuetzalReader::Iterator it = quetzal.begin(); it != quetzal.end(); ++it) {
		if ((*it)._id != MKTAG('Q', 'u', 'i', 'l'))
			continue;

		Common::SeekableReadStream *in = it.getStream();
		in->read(_vm->flagsPtr(), QuillVM::FLAG_COUNT);

		uint32 count = in->readUint32LE();
		Common::Array<byte> &objLoc = _vm->objLoc();
		objLoc.resize(count);
		if (count)
			in->read(&objLoc[0], count);

		delete in;
		return Common::kNoError;
	}

	return Common::kReadingFailed;
}

} // End of namespace Quill
} // End of namespace Glk
