#include <string.h>
#include <unity.h>
#include "plp.h"

// Hand-built PLP1 file: loop=1, version 7, one EFFECT step and one FRAME step.
static uint8_t file[16 + 16 + 703];
static uint32_t fileLen = 0;

static void put8(uint32_t& at, uint8_t v) { file[at++] = v; }
static void put16(uint32_t& at, uint16_t v) { put8(at, v & 0xFF); put8(at, v >> 8); }
static void put32(uint32_t& at, uint32_t v) { put16(at, v & 0xFFFF); put16(at, v >> 16); }

static void buildFile() {
    uint32_t at = 0;
    put8(at, 'P'); put8(at, 'L'); put8(at, 'P'); put8(at, '1');
    put32(at, 7);          // plans_version
    put8(at, 1);           // loop
    put8(at, 2);           // step_count
    put16(at, 0);          // reserved
    put32(at, 0);          // crc, patched below
    // EFFECT: comet, 2100 ms, bri 200, bounce, x16 24, colours
    put8(at, 1); put8(at, 3); put16(at, 2100); put8(at, 200); put8(at, 2); put8(at, 24);
    put8(at, 255); put8(at, 0); put8(at, 192);
    put8(at, 0); put8(at, 229); put8(at, 255);
    put8(at, 0); put8(at, 0); put8(at, 0);
    // FRAME: 800 ms, pixel i = (i, 2i, 3i), fade 10*i, delay i
    put8(at, 2); put16(at, 800);
    for (uint16_t i = 0; i < 100; i++) {
        put8(at, i); put8(at, 2 * i); put8(at, 3 * i);
        put16(at, 10 * i); put16(at, i);
    }
    fileLen = at;
    uint32_t crc = crc32(file + 16, fileLen - 16);
    uint32_t c = 12;
    put32(c, crc);
}

void setUp() { buildFile(); }
void tearDown() {}

void test_crc32_check_value() {
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32((const uint8_t*)"123456789", 9));
}

void test_crc32_can_be_chained() {
    uint32_t part = crc32((const uint8_t*)"1234", 4);
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926, crc32((const uint8_t*)"56789", 5, part));
}

void test_index_reads_header_and_steps() {
    MemorySource src(file, fileLen);
    PlanIndex index;
    TEST_ASSERT_TRUE(planIndex(src, index));
    TEST_ASSERT_EQUAL_UINT32(7, index.version);
    TEST_ASSERT_TRUE(index.loop);
    TEST_ASSERT_EQUAL_UINT8(2, index.stepCount);
    TEST_ASSERT_EQUAL_UINT32(2900, index.totalMs);
    TEST_ASSERT_EQUAL_INT((int)StepKind::Effect, (int)index.kind[0]);
    TEST_ASSERT_EQUAL_INT((int)StepKind::Frame, (int)index.kind[1]);
}

void test_reads_effect_step() {
    MemorySource src(file, fileLen);
    PlanIndex index;
    planIndex(src, index);
    EffectStep s;
    TEST_ASSERT_TRUE(readEffectStep(src, index, 0, s));
    TEST_ASSERT_EQUAL_INT((int)EffectId::Comet, (int)s.effect);
    TEST_ASSERT_EQUAL_UINT16(2100, s.durationMs);
    TEST_ASSERT_EQUAL_UINT8(200, s.brightness);
    TEST_ASSERT_EQUAL_INT((int)Direction::Bounce, (int)s.direction);
    TEST_ASSERT_EQUAL_UINT8(24, s.speedX16);
    TEST_ASSERT_EQUAL_UINT8(192, s.colors[0].b);
    TEST_ASSERT_EQUAL_UINT8(229, s.colors[1].g);
    TEST_ASSERT_FALSE(readEffectStep(src, index, 1, s));  // step 1 is a frame
}

void test_reads_frame_pixels() {
    MemorySource src(file, fileLen);
    PlanIndex index;
    planIndex(src, index);
    static FramePixel px[kPlanPixels];
    TEST_ASSERT_TRUE(readFramePixels(src, index, 1, px));
    TEST_ASSERT_EQUAL_UINT8(42, px[42].color.r);
    TEST_ASSERT_EQUAL_UINT8(84, px[42].color.g);
    TEST_ASSERT_EQUAL_UINT8(126, px[42].color.b);
    TEST_ASSERT_EQUAL_UINT16(420, px[42].fadeMs);
    TEST_ASSERT_EQUAL_UINT16(42, px[42].delayMs);
}

void test_rejects_bad_magic() {
    file[0] = 'X';
    MemorySource src(file, fileLen);
    PlanIndex index;
    TEST_ASSERT_FALSE(planIndex(src, index));
}

void test_rejects_corrupted_body() {
    file[40] ^= 0x01;
    MemorySource src(file, fileLen);
    PlanIndex index;
    TEST_ASSERT_FALSE(planIndex(src, index));
}

void test_rejects_truncated_file() {
    MemorySource src(file, fileLen - 1);
    PlanIndex index;
    TEST_ASSERT_FALSE(planIndex(src, index));
}

void test_rejects_unknown_step_kind_and_effect() {
    PlanIndex index;
    file[16] = 9;  // kind
    uint32_t c = 12;
    uint32_t crc = crc32(file + 16, fileLen - 16);
    put32(c, crc);
    MemorySource a(file, fileLen);
    TEST_ASSERT_FALSE(planIndex(a, index));

    buildFile();
    file[17] = 42;  // effect id
    c = 12;
    crc = crc32(file + 16, fileLen - 16);
    put32(c, crc);
    MemorySource b(file, fileLen);
    TEST_ASSERT_FALSE(planIndex(b, index));
}

void test_rejects_zero_steps() {
    file[9] = 0;
    MemorySource src(file, 16);
    PlanIndex index;
    TEST_ASSERT_FALSE(planIndex(src, index));
}

void test_encode_effect_plan_round_trips() {
    const EffectStep steps[] = {
        {EffectId::Fade, 1500, 255, Direction::Up, 16, {{255, 0, 0}, {40, 0, 0}, {0, 0, 0}}},
        {EffectId::Rainbow, 20480, 128, Direction::Down, 32, {}},
    };
    static uint8_t buf[256];
    uint32_t len = encodeEffectPlan(3, false, steps, 2, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_UINT32(16 + 2 * 16, len);
    TEST_ASSERT_EQUAL_MEMORY("PLP1", buf, 4);

    MemorySource src(buf, len);
    PlanIndex index;
    TEST_ASSERT_TRUE(planIndex(src, index));
    TEST_ASSERT_FALSE(index.loop);
    TEST_ASSERT_EQUAL_UINT32(3, index.version);
    TEST_ASSERT_EQUAL_UINT32(21980, index.totalMs);
    EffectStep s;
    TEST_ASSERT_TRUE(readEffectStep(src, index, 1, s));
    TEST_ASSERT_EQUAL_INT((int)EffectId::Rainbow, (int)s.effect);
    TEST_ASSERT_EQUAL_UINT8(128, s.brightness);
    TEST_ASSERT_EQUAL_INT((int)Direction::Down, (int)s.direction);
    TEST_ASSERT_EQUAL_UINT8(32, s.speedX16);
}

void test_encode_refuses_small_buffer() {
    const EffectStep steps[] = {{EffectId::Off, 100, 255, Direction::Up, 16, {}}};
    uint8_t buf[20];
    TEST_ASSERT_EQUAL_UINT32(0, encodeEffectPlan(1, true, steps, 1, buf, sizeof(buf)));
}

void test_index_keeps_the_file_crc() {
    MemorySource src(file, fileLen);
    PlanIndex index;
    planIndex(src, index);
    TEST_ASSERT_EQUAL_HEX32(crc32(file + 16, fileLen - 16), index.crc);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_check_value);
    RUN_TEST(test_crc32_can_be_chained);
    RUN_TEST(test_index_reads_header_and_steps);
    RUN_TEST(test_reads_effect_step);
    RUN_TEST(test_reads_frame_pixels);
    RUN_TEST(test_rejects_bad_magic);
    RUN_TEST(test_rejects_corrupted_body);
    RUN_TEST(test_rejects_truncated_file);
    RUN_TEST(test_rejects_unknown_step_kind_and_effect);
    RUN_TEST(test_rejects_zero_steps);
    RUN_TEST(test_encode_effect_plan_round_trips);
    RUN_TEST(test_encode_refuses_small_buffer);
    RUN_TEST(test_index_keeps_the_file_crc);
    return UNITY_END();
}
