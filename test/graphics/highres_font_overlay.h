#include <cxxtest/TestSuite.h>

#include "graphics/fonts/highres_font_overlay.h"
#include "graphics/fonts/highres_font_manager.h"
#include "common/rect.h"
#include "test/system/null_osystem.h"

#ifdef USE_FREETYPE2
#include "graphics/fonts/glyph_atlas.h"
#endif

/**
 * These tests intentionally do not depend on a bundled TTF asset, so
 * they exercise the paths that must work identically whether or not
 * this build has USE_FREETYPE2 and a font file available: an overlay
 * that never successfully loads a font must behave as an inert,
 * always-safe no-op, never crash, and always report itself inactive so
 * callers reliably fall back to the legacy raster path.
 *
 * HighResFontOverlay holds a Common::Mutex member, which (like the rest
 * of ScummVM) requires a live g_system to construct, so setUp()/
 * tearDown() install the same null OSystem the audio/common test
 * suites use for the duration of each test.
 */
class HighResFontOverlayTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::install_null_g_system();
#endif
	}

	void tearDown() {
#if NULL_OSYSTEM_IS_AVAILABLE
		Common::uninstall_null_g_system();
#endif
	}

	void test_defaultConstructedOverlayIsInactive() {
		Graphics::HighResFontOverlay overlay;
		TS_ASSERT(!overlay.isActive());
	}

	void test_loadingMissingFontFails() {
		Graphics::HighResFontOverlay overlay;
		bool loaded = overlay.loadTrueTypeFont("this/font/definitely/does/not/exist.ttf", 18);
		TS_ASSERT(!loaded);
		TS_ASSERT(!overlay.isActive());
	}

	void test_enqueueOnInactiveOverlayFails() {
		Graphics::HighResFontOverlay overlay;
		bool queued = overlay.enqueueText("hello", Common::Point(0, 0), 42, 0x00FFFFFF);
		TS_ASSERT(!queued);
	}

	void test_renderAndClearOnInactiveOverlayDoNotCrash() {
		Graphics::HighResFontOverlay overlay;
		overlay.clearQueue();
		overlay.flushGlyphCache();
		overlay.renderOverlay(nullptr);
		// Reaching this point without crashing is the assertion.
		TS_ASSERT(!overlay.isActive());
	}

	void test_managerReturnsSameOverlayForSameId() {
		Graphics::HighResFontManager::FontConfig config;
		config.path = "this/font/definitely/does/not/exist.ttf";
		config.pointSize = 18;
		config.id = "cxxtest_dummy_id";

		Graphics::HighResFontOverlay *first = Graphics::HighResFontManager::instance().loadFont(config);
		Graphics::HighResFontOverlay *second = Graphics::HighResFontManager::instance().loadFont(config);

		TS_ASSERT(first != nullptr);
		TS_ASSERT_EQUALS(first, second);
		TS_ASSERT(!first->isActive());
	}

	void test_managerGetOverlayUnknownIdReturnsNull() {
		TS_ASSERT(Graphics::HighResFontManager::instance().getOverlay("cxxtest_never_registered_id") == nullptr);
	}

#ifdef USE_FREETYPE2
	void test_glyphAtlasConstructionAndFlush() {
		Graphics::GlyphAtlas atlas(64, 64);
		TS_ASSERT_EQUALS(atlas.getWidth(), 64);
		TS_ASSERT_EQUALS(atlas.getHeight(), 64);
		TS_ASSERT(atlas.getPixels() != nullptr);

		// An atlas with nothing packed into it yet is fully transparent.
		bool allZero = true;
		const byte *pixels = atlas.getPixels();
		for (uint16 i = 0; i < 64 * 64; i++) {
			if (pixels[i] != 0) {
				allZero = false;
				break;
			}
		}
		TS_ASSERT(allZero);

		// flush() on an already-empty atlas must not crash.
		atlas.flush();
		TS_ASSERT_EQUALS(atlas.getWidth(), 64);
	}
#endif
};
