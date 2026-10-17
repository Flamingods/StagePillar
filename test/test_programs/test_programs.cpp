#include <stdio.h>
#include <string.h>
#include <unity.h>
#include "program_control.h"

void setUp() {}
void tearDown() {}

void test_no_command_means_no_program() {
    ProgramControl c;
    c.pollOk(0);
    TEST_ASSERT_EQUAL_UINT16(0, c.wanted(100));
}

void test_newer_command_is_applied_older_ignored() {
    ProgramControl c;
    c.pollOk(0);
    TEST_ASSERT_TRUE(c.offer(PillarCommand{5, 7}));
    TEST_ASSERT_EQUAL_UINT16(7, c.wanted(10));
    TEST_ASSERT_FALSE(c.offer(PillarCommand{5, 9}));  // same seq: duplicate
    TEST_ASSERT_FALSE(c.offer(PillarCommand{4, 9}));  // older
    TEST_ASSERT_EQUAL_UINT16(7, c.wanted(10));
    TEST_ASSERT_TRUE(c.offer(PillarCommand{6, 0}));   // release
    TEST_ASSERT_EQUAL_UINT16(0, c.wanted(10));
}

void test_losing_the_pi_pauses_and_resumes() {
    ProgramControl c;
    c.pollOk(1000);
    c.offer(PillarCommand{1, 3});
    TEST_ASSERT_EQUAL_UINT16(3, c.wanted(10999));
    TEST_ASSERT_EQUAL_UINT16(0, c.wanted(11000));  // 10 s without a successful poll
    c.pollOk(15000);
    TEST_ASSERT_EQUAL_UINT16(3, c.wanted(15001));
}

void test_finished_one_shot_stays_finished_until_a_new_command() {
    ProgramControl c;
    c.pollOk(0);
    c.offer(PillarCommand{2, 4});
    c.finished();
    TEST_ASSERT_EQUAL_UINT16(0, c.wanted(10));
    c.pollOk(20000);  // a pause and resume does not replay it
    TEST_ASSERT_EQUAL_UINT16(0, c.wanted(20001));
    c.offer(PillarCommand{3, 4});  // commanded again
    TEST_ASSERT_EQUAL_UINT16(4, c.wanted(20002));
}

void test_reports_the_last_seq() {
    ProgramControl c;
    c.offer(PillarCommand{12, 1});
    TEST_ASSERT_EQUAL_UINT32(12, c.lastSeq());
}

void test_program_index_parsing() {
    ProgramIndex idx;
    TEST_ASSERT_TRUE(parseProgramIndex("{\"version\":8,\"ids\":[1,4,7]}", idx));
    TEST_ASSERT_EQUAL_UINT32(8, idx.version);
    TEST_ASSERT_EQUAL_UINT8(3, idx.count);
    TEST_ASSERT_EQUAL_UINT8(7, idx.ids[2]);
    TEST_ASSERT_TRUE(parseProgramIndex("{\"version\":9,\"ids\":[]}", idx));
    TEST_ASSERT_EQUAL_UINT8(0, idx.count);
}

void test_program_index_rejects_bad_input() {
    ProgramIndex idx;
    TEST_ASSERT_FALSE(parseProgramIndex("{\"ids\":[1]}", idx));
    TEST_ASSERT_FALSE(parseProgramIndex("{\"version\":1,\"ids\":[0]}", idx));    // ids are 1..255
    TEST_ASSERT_FALSE(parseProgramIndex("{\"version\":1,\"ids\":[256]}", idx));
    TEST_ASSERT_FALSE(parseProgramIndex("{\"version\":1,\"ids\":\"x\"}", idx));
    char many[200] = "{\"version\":1,\"ids\":[";
    for (int i = 1; i <= 25; i++) {
        char n[8];
        snprintf(n, sizeof(n), i == 25 ? "%d" : "%d,", i);
        strcat(many, n);
    }
    strcat(many, "]}");
    TEST_ASSERT_FALSE(parseProgramIndex(many, idx));  // more than 24 programs
}

void test_poll_newer_than_the_frame_clock_does_not_pause() {
    // The poll task can record a time a few ms after loop() read millis().
    ProgramControl c;
    c.offer(PillarCommand{1, 6});
    c.pollOk(5005);
    TEST_ASSERT_EQUAL_UINT16(6, c.wanted(5000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_no_command_means_no_program);
    RUN_TEST(test_newer_command_is_applied_older_ignored);
    RUN_TEST(test_losing_the_pi_pauses_and_resumes);
    RUN_TEST(test_finished_one_shot_stays_finished_until_a_new_command);
    RUN_TEST(test_reports_the_last_seq);
    RUN_TEST(test_program_index_parsing);
    RUN_TEST(test_program_index_rejects_bad_input);
    RUN_TEST(test_poll_newer_than_the_frame_clock_does_not_pause);
    return UNITY_END();
}
