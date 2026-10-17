#pragma once
#include <stddef.h>
#include <stdint.h>
#include "led_catalogue.h"

// Reader (and effect-only writer) for the PLP1 plan format defined in the
// StageController pillar LED contract (docs/pillar-led-contract.md, section 5).

constexpr uint16_t kPlanPixels = 100;
constexpr uint8_t kPlanMaxSteps = 64;
constexpr uint32_t kPlanHeaderBytes = 16;
constexpr uint32_t kEffectStepBytes = 16;
constexpr uint32_t kFrameStepBytes = 3 + kPlanPixels * 7;
constexpr uint32_t kPlanMaxBytes = 48 * 1024;

// Standard CRC-32 (zlib.crc32). Pass a previous result as crc to continue it.
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

// Random-access bytes of one plan file: memory for code defaults and tests, flash on the ESP.
struct ByteSource {
    virtual ~ByteSource() {}
    virtual uint32_t size() const = 0;
    virtual bool read(uint32_t offset, uint8_t* dst, uint32_t len) const = 0;
};

struct MemorySource : ByteSource {
    MemorySource(const uint8_t* data, uint32_t len) : data_(data), len_(len) {}
    uint32_t size() const override { return len_; }
    bool read(uint32_t offset, uint8_t* dst, uint32_t len) const override;

private:
    const uint8_t* data_;
    uint32_t len_;
};

enum class StepKind : uint8_t { Effect = 1, Frame = 2 };

// Header plus where each step lives; built once when a plan is loaded.
struct PlanIndex {
    uint32_t version;
    bool loop;
    uint8_t stepCount;
    uint32_t totalMs;
    uint32_t crc;  // the file's body CRC: tells whether two files hold the same plan
    StepKind kind[kPlanMaxSteps];
    uint16_t offset[kPlanMaxSteps];
    uint16_t durationMs[kPlanMaxSteps];
};

struct FramePixel {
    Rgb color;
    uint16_t fadeMs;
    uint16_t delayMs;
};

// Checks magic, size, CRC and every step; fills the index. False = do not play it.
bool planIndex(const ByteSource& src, PlanIndex& out);
bool readEffectStep(const ByteSource& src, const PlanIndex& index, uint8_t step, EffectStep& out);
// out must hold kPlanPixels entries.
bool readFramePixels(const ByteSource& src, const PlanIndex& index, uint8_t step, FramePixel* out);

// Writes a PLP1 file made only of effect steps. Returns its length, or 0 if it does not fit.
uint32_t encodeEffectPlan(uint32_t version, bool loop, const EffectStep* steps, uint8_t count,
                          uint8_t* out, uint32_t capacity);
