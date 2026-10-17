#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "show_events.h"

static ShowEvent show;
static PillarCommand cmd;

static WsMessage parse(const char* json, char side) {
    return parseWsMessage(json, strlen(json), side, show, cmd);
}

void setUp() {
    show = ShowEvent{};
    cmd = PillarCommand{};
}
void tearDown() {}

static const char* kIntro =
    "{\"action\":\"show\",\"timestamp\":\"2026-10-03T20:15:01.123\",\"state\":\"intro\",\"game\":\"showoff\",\"step\":2,"
    "\"poles\":{\"L\":{\"pct\":66,\"color\":\"lime\",\"mode\":\"solid\",\"ms\":null},"
    "\"R\":{\"pct\":33,\"color\":\"blue\",\"mode\":\"pulse\",\"ms\":500}},"
    "\"perimeter\":{\"look\":\"intro\",\"color\":\"blue\",\"side\":\"R\"},\"song\":null,\"thunder\":null}";

void test_show_event_takes_own_side_pole() {
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Show, (int)parse(kIntro, 'L'));
    TEST_ASSERT_EQUAL_INT((int)ShowState::Intro, (int)show.state);
    TEST_ASSERT_EQUAL_INT((int)PoleMode::Solid, (int)show.pole.mode);
    TEST_ASSERT_EQUAL_UINT8(66, show.pole.pct);
    TEST_ASSERT_EQUAL_STRING("lime", show.pole.color);
    TEST_ASSERT_FALSE(show.pole.hasMs);

    TEST_ASSERT_EQUAL_INT((int)WsMessage::Show, (int)parse(kIntro, 'R'));
    TEST_ASSERT_EQUAL_INT((int)PoleMode::Pulse, (int)show.pole.mode);
    TEST_ASSERT_EQUAL_UINT8(33, show.pole.pct);
    TEST_ASSERT_EQUAL_STRING("blue", show.pole.color);
    TEST_ASSERT_TRUE(show.pole.hasMs);
    TEST_ASSERT_EQUAL_UINT32(500, show.pole.ms);
}

void test_glow_reads_level_with_default() {
    parse("{\"action\":\"show\",\"state\":\"playing\",\"poles\":{\"R\":{\"pct\":100,\"color\":\"pink\",\"mode\":\"glow\","
          "\"level\":40,\"ms\":null},\"L\":{\"pct\":0,\"color\":null,\"mode\":\"off\",\"ms\":null}}}", 'R');
    TEST_ASSERT_EQUAL_INT((int)PoleMode::Glow, (int)show.pole.mode);
    TEST_ASSERT_EQUAL_UINT8(40, show.pole.level);
    parse("{\"action\":\"show\",\"state\":\"playing\",\"poles\":{\"R\":{\"pct\":100,\"color\":\"pink\",\"mode\":\"glow\"}}}", 'R');
    TEST_ASSERT_EQUAL_UINT8(35, show.pole.level);
}

void test_off_pole_has_no_colour() {
    parse("{\"action\":\"show\",\"state\":\"idle\",\"poles\":{\"L\":{\"pct\":0,\"color\":null,\"mode\":\"off\",\"ms\":null}}}", 'L');
    TEST_ASSERT_EQUAL_INT((int)ShowState::Idle, (int)show.state);
    TEST_ASSERT_EQUAL_INT((int)PoleMode::Off, (int)show.pole.mode);
    TEST_ASSERT_EQUAL_STRING("", show.pole.color);
}

void test_every_state_and_mode_name() {
    const char* states[] = {"idle", "launching", "intro", "playing", "failing"};
    const ShowState stateIds[] = {ShowState::Idle, ShowState::Launching, ShowState::Intro, ShowState::Playing,
                                  ShowState::Failing};
    char json[200];
    for (int i = 0; i < 5; i++) {
        snprintf(json, sizeof(json), "{\"action\":\"show\",\"state\":\"%s\",\"poles\":{}}", states[i]);
        parse(json, 'L');
        TEST_ASSERT_EQUAL_INT((int)stateIds[i], (int)show.state);
    }
    const char* modes[] = {"off", "glow", "solid", "pulse", "blink", "drain"};
    const PoleMode modeIds[] = {PoleMode::Off, PoleMode::Glow, PoleMode::Solid, PoleMode::Pulse, PoleMode::Blink,
                                PoleMode::Drain};
    for (int i = 0; i < 6; i++) {
        snprintf(json, sizeof(json), "{\"action\":\"show\",\"state\":\"playing\",\"poles\":{\"L\":{\"mode\":\"%s\",\"pct\":50}}}",
                 modes[i]);
        parse(json, 'L');
        TEST_ASSERT_EQUAL_INT((int)modeIds[i], (int)show.pole.mode);
    }
}

void test_unknown_state_is_drawn_and_missing_pole_is_off() {
    parse("{\"action\":\"show\",\"state\":\"encore\",\"poles\":{\"R\":{\"pct\":50,\"color\":\"white\",\"mode\":\"solid\"}}}", 'L');
    TEST_ASSERT_EQUAL_INT((int)ShowState::Other, (int)show.state);
    TEST_ASSERT_EQUAL_INT((int)PoleMode::Off, (int)show.pole.mode);
}

void test_pct_is_clamped() {
    parse("{\"action\":\"show\",\"state\":\"intro\",\"poles\":{\"L\":{\"pct\":150,\"color\":\"pink\",\"mode\":\"solid\"}}}", 'L');
    TEST_ASSERT_EQUAL_UINT8(100, show.pole.pct);
}

void test_pillar_command_for_own_side_only() {
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Command,
                          (int)parse("{\"action\":\"pillar_command\",\"side\":\"R\",\"seq\":41,\"program\":7}", 'R'));
    TEST_ASSERT_EQUAL_UINT32(41, cmd.seq);
    TEST_ASSERT_EQUAL_UINT16(7, cmd.program);
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Ignore,
                          (int)parse("{\"action\":\"pillar_command\",\"side\":\"R\",\"seq\":42,\"program\":3}", 'L'));
}

void test_other_messages_are_ignored() {
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Ignore, (int)parse("{\"action\":\"start\",\"playlist_name\":\"duet\"}", 'L'));
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Ignore, (int)parse("{\"action\":\"cue\",\"cue\":\"claps\",\"side\":\"L\"}", 'L'));
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Ignore, (int)parse("not json", 'L'));
    TEST_ASSERT_EQUAL_INT((int)WsMessage::Ignore, (int)parse("{\"action\":\"pillar_command\",\"side\":\"L\"}", 'L'));
}

void test_state_reply_command_backup() {
    PillarCommand c{};
    TEST_ASSERT_TRUE(parseStateCommand("{\"current_song\":null,\"pillar_command\":{\"seq\":9,\"program\":0}}", c));
    TEST_ASSERT_EQUAL_UINT32(9, c.seq);
    TEST_ASSERT_EQUAL_UINT16(0, c.program);
    TEST_ASSERT_FALSE(parseStateCommand("{\"current_song\":null}", c));
    TEST_ASSERT_FALSE(parseStateCommand("{\"pillar_command\":null}", c));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_show_event_takes_own_side_pole);
    RUN_TEST(test_glow_reads_level_with_default);
    RUN_TEST(test_off_pole_has_no_colour);
    RUN_TEST(test_every_state_and_mode_name);
    RUN_TEST(test_unknown_state_is_drawn_and_missing_pole_is_off);
    RUN_TEST(test_pct_is_clamped);
    RUN_TEST(test_pillar_command_for_own_side_only);
    RUN_TEST(test_other_messages_are_ignored);
    RUN_TEST(test_state_reply_command_backup);
    return UNITY_END();
}
