#include <unity.h>
#include "urls.h"

static char buf[160];

void setUp() {}
void tearDown() {}

void test_poll_path_carries_side_and_version() {
    TEST_ASSERT_TRUE(pollPath(buf, sizeof(buf), 'R', 17));
    TEST_ASSERT_EQUAL_STRING("/api/player/state?client=pillar&side=R&running_version=17", buf);
}

void test_press_body_carries_side_taps_and_first_press_age() {
    TEST_ASSERT_TRUE(pressBody(buf, sizeof(buf), "claps", 'L', 2, 512));
    TEST_ASSERT_EQUAL_STRING("{\"action\":\"claps\",\"side\":\"L\",\"taps\":2,\"first_press_ago_ms\":512}", buf);
}

void test_builders_refuse_a_buffer_that_is_too_small() {
    char small[20];
    TEST_ASSERT_FALSE(pollPath(small, sizeof(small), 'L', 0));
    TEST_ASSERT_FALSE(pressBody(small, sizeof(small), "special", 'R', 3, 0));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_poll_path_carries_side_and_version);
    RUN_TEST(test_press_body_carries_side_taps_and_first_press_age);
    RUN_TEST(test_builders_refuse_a_buffer_that_is_too_small);
    return UNITY_END();
}
