#include <unity.h>
#include <utility>
#include <vector>
#include "gesture.h"

// Drives the detector one millisecond at a time and records each gesture with its time.
struct Sim {
    GestureDetector detector;
    uint32_t t;
    std::vector<std::pair<Gesture, uint32_t>> out;

    explicit Sim(uint32_t start = 0) : t(start) {}

    void hold(bool pressed, uint32_t ms) {
        for (uint32_t i = 0; i < ms; i++, t++) {
            Gesture g = detector.update(pressed, t);
            if (g != Gesture::None) out.push_back({g, t});
        }
    }

    void tap() {
        hold(true, 100);
        hold(false, 100);
    }

    void taps(int n) {
        hold(false, 10);
        for (int i = 0; i < n; i++) tap();
        hold(false, 1000);
    }
};

static void assertOnly(const Sim& s, Gesture expected) {
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, s.out.size(), "expected exactly one gesture");
    TEST_ASSERT_EQUAL_INT((int)expected, (int)s.out[0].first);
}

void setUp() {}
void tearDown() {}

void test_one_tap_is_single() { Sim s; s.taps(1); assertOnly(s, Gesture::Single); }
void test_two_taps_is_double() { Sim s; s.taps(2); assertOnly(s, Gesture::Double); }
void test_three_taps_is_triple() { Sim s; s.taps(3); assertOnly(s, Gesture::Triple); }
void test_four_taps_is_quad() { Sim s; s.taps(4); assertOnly(s, Gesture::Quad); }

void test_five_taps_is_many() { Sim s; s.taps(5); assertOnly(s, Gesture::Many); }
void test_eight_taps_is_many() { Sim s; s.taps(8); assertOnly(s, Gesture::Many); }

void test_detector_recovers_after_many_taps() {
    Sim s;
    s.taps(8);
    s.taps(1);
    TEST_ASSERT_EQUAL_UINT32(2, s.out.size());
    TEST_ASSERT_EQUAL_INT((int)Gesture::Single, (int)s.out[1].first);
}

void test_bounce_shorter_than_debounce_is_ignored() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 20);
    s.hold(false, 1000);
    TEST_ASSERT_EQUAL_UINT32(0, s.out.size());
}

void test_bounce_inside_a_press_counts_once() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 50);
    s.hold(false, 10);
    s.hold(true, 50);
    s.hold(false, 1000);
    assertOnly(s, Gesture::Single);
}

void test_gap_ends_gesture_exactly_400ms_after_release() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 100);
    uint32_t release = s.t;
    s.hold(false, 400);  // ticks release .. release+399
    TEST_ASSERT_EQUAL_UINT32(0, s.out.size());
    s.hold(false, 1);    // tick release+400
    assertOnly(s, Gesture::Single);
    TEST_ASSERT_EQUAL_UINT32(release + 400, s.out[0].second);
}

void test_press_399ms_after_release_continues_gesture() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 100);
    s.hold(false, 399);
    s.hold(true, 100);
    s.hold(false, 1000);
    assertOnly(s, Gesture::Double);
}

void test_long_press_fires_at_1500ms_while_held() {
    Sim s;
    s.hold(false, 10);
    uint32_t pressed = s.t;
    s.hold(true, 2000);
    assertOnly(s, Gesture::Long);
    TEST_ASSERT_EQUAL_UINT32(pressed + 1500, s.out[0].second);
    s.hold(false, 1000);
    assertOnly(s, Gesture::Long);  // release emits nothing
}

void test_hold_just_under_long_is_single() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 1499);
    s.hold(false, 1000);
    assertOnly(s, Gesture::Single);
}

void test_taps_then_long_press_is_only_long() {
    Sim s;
    s.hold(false, 10);
    s.tap();
    s.tap();
    s.hold(true, 2000);
    s.hold(false, 1000);
    assertOnly(s, Gesture::Long);
}

void test_held_forever_emits_one_long() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 10000);
    s.hold(false, 1000);
    assertOnly(s, Gesture::Long);
}

void test_tap_after_long_press_is_single() {
    Sim s;
    s.hold(false, 10);
    s.hold(true, 2000);
    s.hold(false, 1000);
    s.taps(1);
    TEST_ASSERT_EQUAL_UINT32(2, s.out.size());
    TEST_ASSERT_EQUAL_INT((int)Gesture::Single, (int)s.out[1].first);
}

void test_works_across_millis_rollover() {
    Sim s(0xFFFFFFFFu - 50);
    s.taps(2);
    assertOnly(s, Gesture::Double);
}

void test_reports_real_tap_count_and_first_press_time() {
    Sim s;
    s.hold(false, 10);            // first press edge at t=10
    for (int i = 0; i < 3; i++) s.tap();
    s.hold(false, 1000);
    assertOnly(s, Gesture::Triple);
    TEST_ASSERT_EQUAL_UINT8(3, s.detector.lastTaps());
    TEST_ASSERT_EQUAL_UINT32(10, s.detector.lastFirstPressMs());
}

void test_many_reports_its_real_count() {
    Sim s;
    s.taps(7);
    assertOnly(s, Gesture::Many);
    TEST_ASSERT_EQUAL_UINT8(7, s.detector.lastTaps());
}

void test_tap_count_is_capped_at_20() {
    Sim s;
    s.taps(25);
    assertOnly(s, Gesture::Many);
    TEST_ASSERT_EQUAL_UINT8(20, s.detector.lastTaps());
}

void test_long_press_reports_one_tap_at_its_press_edge() {
    Sim s;
    s.hold(false, 10);
    s.tap();                      // discarded by the long press
    uint32_t edge = s.t;
    s.hold(true, 2000);
    assertOnly(s, Gesture::Long);
    TEST_ASSERT_EQUAL_UINT8(1, s.detector.lastTaps());
    TEST_ASSERT_EQUAL_UINT32(edge, s.detector.lastFirstPressMs());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_one_tap_is_single);
    RUN_TEST(test_two_taps_is_double);
    RUN_TEST(test_three_taps_is_triple);
    RUN_TEST(test_four_taps_is_quad);
    RUN_TEST(test_five_taps_is_many);
    RUN_TEST(test_eight_taps_is_many);
    RUN_TEST(test_detector_recovers_after_many_taps);
    RUN_TEST(test_bounce_shorter_than_debounce_is_ignored);
    RUN_TEST(test_bounce_inside_a_press_counts_once);
    RUN_TEST(test_gap_ends_gesture_exactly_400ms_after_release);
    RUN_TEST(test_press_399ms_after_release_continues_gesture);
    RUN_TEST(test_long_press_fires_at_1500ms_while_held);
    RUN_TEST(test_hold_just_under_long_is_single);
    RUN_TEST(test_taps_then_long_press_is_only_long);
    RUN_TEST(test_held_forever_emits_one_long);
    RUN_TEST(test_tap_after_long_press_is_single);
    RUN_TEST(test_works_across_millis_rollover);
    RUN_TEST(test_reports_real_tap_count_and_first_press_time);
    RUN_TEST(test_many_reports_its_real_count);
    RUN_TEST(test_tap_count_is_capped_at_20);
    RUN_TEST(test_long_press_reports_one_tap_at_its_press_edge);
    return UNITY_END();
}
