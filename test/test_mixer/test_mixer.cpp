#include <string.h>
#include <unity.h>
#include "pillar_mixer.h"

static const uint16_t N = kPlanPixels;
static Rgb frame[N];
static Rgb expected[N];

static bool sameFrame() { return memcmp(frame, expected, sizeof(frame)) == 0; }

static ShowEvent event(ShowState state, PoleMode mode, uint8_t pct, uint32_t ms = 0) {
    ShowEvent e;
    e.state = state;
    e.pole.mode = mode;
    e.pole.pct = pct;
    e.pole.hasMs = ms > 0;
    e.pole.ms = ms;
    strcpy(e.pole.color, "pink");
    return e;
}

// What the player alone would draw at `now` (a fresh player follows the same timeline).
static void playerAlone(uint32_t now) {
    PillarPlayer p;
    p.render(now, expected, N);
}

void setUp() {
    memset(frame, 0, sizeof(frame));
    memset(expected, 0, sizeof(expected));
}
void tearDown() {}

void test_without_show_the_player_draws() {
    PillarPlayer player;
    PillarMixer mixer(player);
    mixer.render(5000, frame, N);
    playerAlone(5000);
    TEST_ASSERT_TRUE(sameFrame());
}

void test_show_idle_falls_through_to_the_player() {
    PillarPlayer player;
    PillarMixer mixer(player);
    mixer.setShowConnected(true);
    mixer.setShowEvent(event(ShowState::Idle, PoleMode::Off, 0), 1000);
    mixer.render(5000, frame, N);
    playerAlone(5000);
    TEST_ASSERT_TRUE(sameFrame());
    TEST_ASSERT_FALSE(mixer.showOwnsLeds());
}

void test_game_state_draws_the_pole() {
    PillarPlayer player;
    PillarMixer mixer(player);
    mixer.setShowConnected(true);
    ShowEvent e = event(ShowState::Intro, PoleMode::Solid, 66);
    mixer.setShowEvent(e, 1000);
    mixer.render(1500, frame, N);
    TEST_ASSERT_TRUE(mixer.showOwnsLeds());
    renderPole(e.pole, 500, 1500, defaultPalette(), expected, N);
    TEST_ASSERT_TRUE(sameFrame());
}

void test_drain_is_timed_from_when_the_event_arrived() {
    PillarPlayer player;
    PillarMixer mixer(player);
    mixer.setShowConnected(true);
    ShowEvent e = event(ShowState::Failing, PoleMode::Drain, 100, 1000);
    mixer.setShowEvent(e, 2000);
    mixer.render(2500, frame, N);
    renderPole(e.pole, 500, 2500, defaultPalette(), expected, N);
    TEST_ASSERT_TRUE(sameFrame());
}

void test_losing_the_socket_returns_to_the_player() {
    PillarPlayer player;
    PillarMixer mixer(player);
    mixer.setShowConnected(true);
    mixer.setShowEvent(event(ShowState::Playing, PoleMode::Glow, 100), 1000);
    mixer.setShowConnected(false);
    TEST_ASSERT_FALSE(mixer.showOwnsLeds());
    mixer.render(5000, frame, N);
    playerAlone(5000);
    TEST_ASSERT_TRUE(sameFrame());
}

void test_preview_is_drawn_over_a_game() {
    static uint8_t buf[64];
    const EffectStep steps[] = {{EffectId::Solid, 3000, 255, Direction::Up, 16, {{0, 0, 255}}}};
    uint32_t len = encodeEffectPlan(9, true, steps, 1, buf, sizeof(buf));
    static MemorySource src(buf, len);
    static PlanIndex index;
    planIndex(src, index);

    PillarPlayer player;
    PillarMixer mixer(player);
    mixer.setShowConnected(true);
    mixer.setShowEvent(event(ShowState::Intro, PoleMode::Solid, 100), 1000);
    player.playPreview(Plan{&src, &index}, 1100);
    mixer.render(1200, frame, N);
    TEST_ASSERT_EQUAL_UINT8(255, frame[50].b);
    TEST_ASSERT_EQUAL_UINT8(0, frame[50].r);
}

void test_server_palette_is_used() {
    PillarPlayer player;
    PillarMixer mixer(player);
    Palette p = defaultPalette();
    paletteFind(p, "pink")->gain = 0.0f;  // pink drawn black
    mixer.setPalette(p);
    mixer.setShowConnected(true);
    mixer.setShowEvent(event(ShowState::Intro, PoleMode::Solid, 100), 0);
    mixer.render(100, frame, N);
    TEST_ASSERT_EQUAL_UINT8(0, frame[10].r);
    TEST_ASSERT_EQUAL_UINT32(0, mixer.paletteVersion());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_without_show_the_player_draws);
    RUN_TEST(test_show_idle_falls_through_to_the_player);
    RUN_TEST(test_game_state_draws_the_pole);
    RUN_TEST(test_drain_is_timed_from_when_the_event_arrived);
    RUN_TEST(test_losing_the_socket_returns_to_the_player);
    RUN_TEST(test_preview_is_drawn_over_a_game);
    RUN_TEST(test_server_palette_is_used);
    return UNITY_END();
}
