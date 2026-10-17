#include <unity.h>
#include "urls.h"

static char buf[160];

void setUp() {}
void tearDown() {}

void test_poll_path_carries_pillar_id_and_version() {
    TEST_ASSERT_TRUE(pollPath(buf, sizeof(buf), 2, 17));
    TEST_ASSERT_EQUAL_STRING("/api/player/state?client=pillar&pillar_id=2&running_version=17", buf);
}

void test_press_path_carries_pillar_id() {
    TEST_ASSERT_TRUE(pressPath(buf, sizeof(buf), "claps", 1));
    TEST_ASSERT_EQUAL_STRING("/api/buttons/press/claps?pillar_id=1", buf);
}

void test_paths_refuse_a_buffer_that_is_too_small() {
    char small[20];
    TEST_ASSERT_FALSE(pollPath(small, sizeof(small), 1, 0));
    TEST_ASSERT_FALSE(pressPath(small, sizeof(small), "special", 1));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_poll_path_carries_pillar_id_and_version);
    RUN_TEST(test_press_path_carries_pillar_id);
    RUN_TEST(test_paths_refuse_a_buffer_that_is_too_small);
    return UNITY_END();
}
