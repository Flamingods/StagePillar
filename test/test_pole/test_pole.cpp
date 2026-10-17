#include <math.h>
#include <string.h>
#include <unity.h>
#include "pole_render.h"
#include "show_gate.h"

static const uint16_t N = 100;
static Rgb frame[N];

static PoleState pole(PoleMode mode, uint8_t pct, const char* color = "lime") {
    PoleState p;
    p.mode = mode;
    p.pct = pct;
    strncpy(p.color, color, sizeof(p.color) - 1);
    return p;
}

static uint16_t lit() {
    uint16_t n = 0;
    for (uint16_t i = 0; i < N; i++) if (frame[i].r || frame[i].g || frame[i].b) n++;
    return n;
}

// Tom's emulator drift (StageController frontend/src/show/stageLook.ts), computed independently.
static void expectedDrift(const float ramp[3][3], float periodS, uint16_t i, float t, float gain, float out[3]) {
    float k = 0.5f + 0.5f * sinf(i * 0.33f - (t * 6.2831853f) / periodS);
    for (int c = 0; c < 3; c++) {
        float v = k < 0.5f ? ramp[0][c] + (ramp[1][c] - ramp[0][c]) * (k * 2)
                           : ramp[1][c] + (ramp[2][c] - ramp[1][c]) * ((k - 0.5f) * 2);
        out[c] = fminf(255.0f, v * gain);
    }
}

static const float kLime[3][3] = {{70, 190, 0}, {125, 255, 0}, {205, 255, 130}};

void setUp() { memset(frame, 0, sizeof(frame)); }
void tearDown() {}

void test_solid_fills_from_the_bottom() {
    renderPole(pole(PoleMode::Solid, 33), 0, 1000, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(33, lit());
    TEST_ASSERT_TRUE(frame[0].g > 0);
    TEST_ASSERT_TRUE(frame[33].g == 0);
}

void test_off_and_missing_colour_are_dark() {
    renderPole(pole(PoleMode::Off, 100), 0, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(0, lit());
    renderPole(pole(PoleMode::Solid, 100, ""), 0, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(0, lit());
}

void test_drain_falls_from_pct_over_ms() {
    PoleState p = pole(PoleMode::Drain, 100);
    p.hasMs = true;
    p.ms = 500;
    renderPole(p, 0, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(100, lit());
    renderPole(p, 250, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(50, lit());
    renderPole(p, 600, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(0, lit());
}

void test_blink_is_250ms_on_250ms_off() {
    PoleState p = pole(PoleMode::Blink, 50);
    renderPole(p, 0, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(50, lit());
    renderPole(p, 300, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(0, lit());
    renderPole(p, 520, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(50, lit());
}

void test_pulse_eases_between_10_and_100_percent() {
    PoleState p = pole(PoleMode::Pulse, 100, "white");
    float c[3];
    const float kWhite[3][3] = {{190, 190, 205}, {255, 255, 255}, {255, 255, 255}};
    expectedDrift(kWhite, 2.8f, 0, 0, 1.0f, c);
    renderPole(p, 0, 0, defaultPalette(), frame, N);  // default 500 ms: dimmest at 0
    TEST_ASSERT_INT_WITHIN(1, lroundf(c[0] * 0.1f), frame[0].r);
    renderPole(p, 250, 0, defaultPalette(), frame, N);  // brightest at half a period
    TEST_ASSERT_INT_WITHIN(1, lroundf(c[0]), frame[0].r);
    p.hasMs = true;
    p.ms = 1000;
    renderPole(p, 500, 0, defaultPalette(), frame, N);  // event's own period
    TEST_ASSERT_INT_WITHIN(1, lroundf(c[0]), frame[0].r);
}

void test_glow_is_full_height_at_level() {
    PoleState p = pole(PoleMode::Glow, 100);
    p.level = 40;
    float c[3];
    expectedDrift(kLime, 2.8f, 7, 0.5f, 1.0f, c);
    renderPole(p, 0, 500, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT16(100, lit());
    TEST_ASSERT_INT_WITHIN(1, lroundf(c[1] * 0.4f), frame[7].g);
}

void test_colour_drifts_along_the_strip_like_the_emulator() {
    float c[3];
    renderPole(pole(PoleMode::Solid, 100), 0, 1234, defaultPalette(), frame, N);
    const uint16_t leds[] = {0, 10, 55, 99};
    for (uint16_t i : leds) {
        expectedDrift(kLime, 2.8f, i, 1.234f, 1.0f, c);
        TEST_ASSERT_INT_WITHIN(1, lroundf(c[0]), frame[i].r);
        TEST_ASSERT_INT_WITHIN(1, lroundf(c[1]), frame[i].g);
        TEST_ASSERT_INT_WITHIN(1, lroundf(c[2]), frame[i].b);
    }
}

void test_pink_uses_its_own_drift_period() {
    const float kPink[3][3] = {{196, 0, 122}, {255, 20, 147}, {255, 106, 193}};
    float c[3];
    expectedDrift(kPink, 2.0f, 20, 0.7f, 1.0f, c);
    renderPole(pole(PoleMode::Solid, 100, "pink"), 0, 700, defaultPalette(), frame, N);
    TEST_ASSERT_INT_WITHIN(1, lroundf(c[2]), frame[20].b);
}

void test_gain_scales_and_caps_at_255() {
    Palette p = defaultPalette();
    PaletteColor* lime = paletteFind(p, "lime");
    TEST_ASSERT_NOT_NULL(lime);
    lime->gain = 2.0f;
    renderPole(pole(PoleMode::Solid, 100), 0, 0, p, frame, N);
    for (uint16_t i = 0; i < N; i++) TEST_ASSERT_EQUAL_UINT8(255, frame[i].g);
}

void test_unknown_colour_draws_white() {
    renderPole(pole(PoleMode::Solid, 10, "teal"), 0, 0, defaultPalette(), frame, N);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].r);
    TEST_ASSERT_EQUAL_UINT8(255, frame[0].b);
}

void test_palette_from_server_json() {
    Palette p;
    const char* json =
        "{\"version\":4,\"colors\":{\"pink\":{\"ramp\":[[1,2,3],[4,5,6],[7,8,9]],\"gain\":1.5,\"drift_ms\":1000},"
        "\"lime\":{\"ramp\":[[10,20,30],[40,50,60],[70,80,90]],\"gain\":1,\"drift_ms\":3000}}}";
    TEST_ASSERT_TRUE(parsePalette(json, p));
    TEST_ASSERT_EQUAL_UINT32(4, p.version);
    PaletteColor* pink = paletteFind(p, "pink");
    TEST_ASSERT_NOT_NULL(pink);
    TEST_ASSERT_EQUAL_UINT8(8, pink->ramp[2].g);
    TEST_ASSERT_EQUAL_FLOAT(1.5f, pink->gain);
    TEST_ASSERT_EQUAL_UINT32(1000, pink->driftMs);
    TEST_ASSERT_NULL(paletteFind(p, "blue"));
    TEST_ASSERT_FALSE(parsePalette("{\"version\":1}", p));
    TEST_ASSERT_FALSE(parsePalette("nope", p));
}

void test_live_server_palette_parses() {
    // Captured from the Pi's GET /api/show/palette (2026-10-17); version is above 2^31.
    const char* json =
        "{\"version\":3788725316,\"colors\":{\"pink\":{\"ramp\":[[196,0,122],[255,20,147],[255,106,193]],"
        "\"gain\":1.0,\"drift_ms\":2000},\"lime\":{\"ramp\":[[70,190,0],[125,255,0],[205,255,130]],\"gain\":1.0,"
        "\"drift_ms\":2800},\"blue\":{\"ramp\":[[12,50,215],[20,80,255],[96,150,255]],\"gain\":1.0,\"drift_ms\":2800},"
        "\"white\":{\"ramp\":[[190,190,205],[255,255,255],[255,255,255]],\"gain\":1.0,\"drift_ms\":2800}}}";
    Palette p;
    TEST_ASSERT_TRUE(parsePalette(json, p));
    TEST_ASSERT_EQUAL_UINT32(3788725316u, p.version);
    TEST_ASSERT_EQUAL_UINT8(4, p.count);
    TEST_ASSERT_EQUAL_UINT32(2000, paletteFind(p, "pink")->driftMs);
    TEST_ASSERT_EQUAL_UINT8(205, paletteFind(p, "white")->ramp[0].b);
}

void test_large_palette_version_in_state_reply() {
    uint32_t v = 0;
    TEST_ASSERT_TRUE(parseUintField("{\"show_palette_version\":3788725316}", "show_palette_version", v));
    TEST_ASSERT_EQUAL_UINT32(3788725316u, v);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_solid_fills_from_the_bottom);
    RUN_TEST(test_off_and_missing_colour_are_dark);
    RUN_TEST(test_drain_falls_from_pct_over_ms);
    RUN_TEST(test_blink_is_250ms_on_250ms_off);
    RUN_TEST(test_pulse_eases_between_10_and_100_percent);
    RUN_TEST(test_glow_is_full_height_at_level);
    RUN_TEST(test_colour_drifts_along_the_strip_like_the_emulator);
    RUN_TEST(test_pink_uses_its_own_drift_period);
    RUN_TEST(test_gain_scales_and_caps_at_255);
    RUN_TEST(test_unknown_colour_draws_white);
    RUN_TEST(test_palette_from_server_json);
    RUN_TEST(test_live_server_palette_parses);
    RUN_TEST(test_large_palette_version_in_state_reply);
    return UNITY_END();
}
