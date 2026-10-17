#include <unity.h>
#include "show_gate.h"

void setUp() {}
void tearDown() {}

void test_song_loaded_is_running() {
    TEST_ASSERT_TRUE(parseShowRunning("{\"current_song\":{\"id\":3,\"title\":\"x\"},\"is_playing\":true}"));
    TEST_ASSERT_TRUE(parseShowRunning("{\"current_song\": {\"id\": 3}}"));
}

void test_paused_song_is_running() {
    TEST_ASSERT_TRUE(parseShowRunning("{\"current_song\":{\"id\":3},\"is_playing\":false}"));
}

void test_no_song_is_not_running() {
    TEST_ASSERT_FALSE(parseShowRunning("{\"current_song\":null,\"queue_length\":5,\"is_playing\":false}"));
    TEST_ASSERT_FALSE(parseShowRunning("{\"current_song\": null}"));
}

void test_restart_glitch_without_song_is_not_running() {
    // Right after a Pi service restart is_playing can be true with no song.
    TEST_ASSERT_FALSE(parseShowRunning("{\"current_song\":null,\"is_playing\":true}"));
}

void test_garbage_is_not_running() {
    TEST_ASSERT_FALSE(parseShowRunning(""));
    TEST_ASSERT_FALSE(parseShowRunning(nullptr));
    TEST_ASSERT_FALSE(parseShowRunning("{\"detail\":\"Not Found\"}"));
    TEST_ASSERT_FALSE(parseShowRunning("{\"current_song\""));
    TEST_ASSERT_FALSE(parseShowRunning("{\"current_song\":"));
}

void test_reads_plans_version_and_preview_id() {
    const char* body = "{\"current_song\":null,\"pillar_plans_version\":17,\"pillar_preview_id\": 4}";
    uint32_t v = 0;
    TEST_ASSERT_TRUE(parseUintField(body, "pillar_plans_version", v));
    TEST_ASSERT_EQUAL_UINT32(17, v);
    TEST_ASSERT_TRUE(parseUintField(body, "pillar_preview_id", v));
    TEST_ASSERT_EQUAL_UINT32(4, v);
}

void test_missing_or_bad_uint_field_is_rejected() {
    uint32_t v = 99;
    TEST_ASSERT_FALSE(parseUintField("{\"current_song\":null}", "pillar_plans_version", v));
    TEST_ASSERT_FALSE(parseUintField("{\"pillar_plans_version\":null}", "pillar_plans_version", v));
    TEST_ASSERT_FALSE(parseUintField("{\"pillar_plans_version\":-3}", "pillar_plans_version", v));
    TEST_ASSERT_FALSE(parseUintField(nullptr, "pillar_plans_version", v));
    TEST_ASSERT_EQUAL_UINT32(99, v);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_song_loaded_is_running);
    RUN_TEST(test_paused_song_is_running);
    RUN_TEST(test_no_song_is_not_running);
    RUN_TEST(test_restart_glitch_without_song_is_not_running);
    RUN_TEST(test_garbage_is_not_running);
    RUN_TEST(test_reads_plans_version_and_preview_id);
    RUN_TEST(test_missing_or_bad_uint_field_is_rejected);
    return UNITY_END();
}
