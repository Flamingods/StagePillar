#pragma once
#include <stdint.h>
#include "plp.h"

// A validated plan: its bytes and the index built from them.
struct Plan {
    const ByteSource* src;
    const PlanIndex* index;
};

// Plays one plan over time. Reads only the current step (at most one frame) from
// its source, when the step starts.
class SequenceRenderer {
public:
    void start(Plan plan, uint32_t nowMs, uint32_t seed);
    // Point at an identical copy of the same plan without restarting it.
    void retarget(Plan plan) { plan_ = plan; }

    // frame must hold what the strip currently shows; it is overwritten with the next
    // frame. Frame steps fade from a snapshot of it taken when the step starts.
    // Returns false once a one-shot plan is over, leaving frame untouched.
    bool render(uint32_t nowMs, Rgb* frame, uint16_t count);

private:
    void load(uint8_t step, const Rgb* frame, uint16_t count);

    Plan plan_{nullptr, nullptr};
    uint32_t startMs_ = 0;
    uint32_t seed_ = 0;
    uint32_t loadedPass_ = 0;
    int16_t loadedStep_ = -1;
    bool loadOk_ = false;
    EffectStep effect_{};
    FramePixel pixels_[kPlanPixels];
    Rgb snapshot_[kPlanPixels];
};
