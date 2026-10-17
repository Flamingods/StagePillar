#include "pillar_player.h"

const Slot kSlots[kStoredSlots] = {Slot::Idle, Slot::Start, Slot::Claps, Slot::Special, Slot::Skip, Slot::Stop};

struct DefaultSlot {
    bool loop;
    EffectStep step;
};

// Index order matches kSlots. Idle lasts exactly 256 LEDs of rainbow shift so the
// pattern is seamless when it loops.
static const DefaultSlot kDefaults[kStoredSlots] = {
    {true, {EffectId::Rainbow, 256 * 80, 255, Direction::Up, 16, {}}},
    {true, {EffectId::Comet, 2100, 255, Direction::Bounce, 24, {{255, 0, 192}, {0, 229, 255}, {255, 176, 0}}}},
    {false, {EffectId::Sparkle, 1200, 255, Direction::Up, 16, {{255, 255, 255}}}},
    {false, {EffectId::Pulse, 1500, 255, Direction::Up, 16, {{0, 0, 255}, {128, 0, 255}, {255, 0, 192}}}},
    {false, {EffectId::Band, 500, 255, Direction::Up, 16, {{0, 255, 255}}}},
    {false, {EffectId::Fade, 1500, 255, Direction::Up, 16, {{255, 0, 0}, {40, 0, 0}}}},
};

static int slotIndex(Slot slot) {
    for (int i = 0; i < kStoredSlots; i++) {
        if (kSlots[i] == slot) return i;
    }
    return 0;
}

Slot slotFor(Gesture gesture) {
    switch (gesture) {
        case Gesture::Single: return Slot::Start;
        case Gesture::Double: return Slot::Claps;
        case Gesture::Triple:
        case Gesture::Many:   return Slot::Special;
        case Gesture::Quad:   return Slot::Skip;
        case Gesture::Long:   return Slot::Stop;
        default:              return Slot::None;
    }
}

const char* slotName(Slot slot) {
    switch (slot) {
        case Slot::Idle:    return "idle";
        case Slot::Start:   return "start";
        case Slot::Claps:   return "claps";
        case Slot::Special: return "special";
        case Slot::Skip:    return "skip";
        case Slot::Stop:    return "stop";
        case Slot::Preview: return "preview";
        default:            return "none";
    }
}

// Defaults are encoded as real PLP1 files so they go through the same reader as uploads.
Plan defaultPlan(Slot slot) {
    static const uint32_t kLen = kPlanHeaderBytes + kEffectStepBytes;
    static uint8_t bufs[kStoredSlots][kLen];
    static const MemorySource sources[kStoredSlots] = {
        MemorySource(bufs[0], kLen), MemorySource(bufs[1], kLen), MemorySource(bufs[2], kLen),
        MemorySource(bufs[3], kLen), MemorySource(bufs[4], kLen), MemorySource(bufs[5], kLen),
    };
    static PlanIndex indexes[kStoredSlots];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < kStoredSlots; i++) {
            encodeEffectPlan(0, kDefaults[i].loop, &kDefaults[i].step, 1, bufs[i], kLen);
            planIndex(sources[i], indexes[i]);
        }
        built = true;
    }
    int i = slotIndex(slot);
    return Plan{&sources[i], &indexes[i]};
}

PillarPlayer::PillarPlayer(Lookup lookup) : lookup_(lookup) {
    play(Slot::Idle, 0);
}

// The server expires preview ids after 60 s; end locally too in case polls stop.
static const uint32_t kPreviewMaxMs = 60000;

void PillarPlayer::play(Slot slot, uint32_t nowMs) {
    active_ = slot;
    activeStartMs_ = nowMs;
    Plan plan = slot == Slot::Preview ? preview_ : slot == Slot::Program ? program_ : lookup_(slot);
    renderer_.start(plan, nowMs, nowMs);
}

void PillarPlayer::trigger(Slot slot, uint32_t nowMs) {
    if (slot == Slot::None || slot == Slot::Preview || slot == Slot::Program) return;
    if (slot == Slot::Start) {
        running_ = true;
        // The play loop never ends by itself, so while a program is commanded a single
        // tap (a launch tap, a thunder steal) keeps the program instead of replacing it.
        if (programActive_) {
            if (active_ != Slot::Program && active_ != Slot::Preview) play(Slot::Program, nowMs);
            return;
        }
    }
    if (slot == Slot::Stop) running_ = false;
    play(slot, nowMs);
}

void PillarPlayer::setShowRunning(bool running, uint32_t nowMs) {
    if (running == running_) return;
    running_ = running;
    if (active_ == Slot::Preview) return;  // the preview keeps playing; base applies after it
    if (running) {
        // A gesture in progress finishes first and then falls back to the play loop;
        // a program stays the base.
        if (active_ == Slot::Idle) play(base(), nowMs);
    } else if (active_ != Slot::Stop) {
        play(Slot::Stop, nowMs);
    }
}

void PillarPlayer::playPreview(Plan plan, uint32_t nowMs) {
    preview_ = plan;
    play(Slot::Preview, nowMs);
}

void PillarPlayer::setProgram(Plan plan, uint32_t nowMs) {
    program_ = plan;
    programActive_ = true;
    programFinished_ = false;
    if (active_ != Slot::Preview) play(Slot::Program, nowMs);
}

void PillarPlayer::clearProgram(uint32_t nowMs) {
    if (!programActive_) return;
    programActive_ = false;
    if (active_ == Slot::Program) play(base(), nowMs);
}

void PillarPlayer::updateProgramPlan(Plan plan) {
    program_ = plan;
    if (active_ == Slot::Program) renderer_.retarget(plan);
}

bool PillarPlayer::takeProgramFinished() {
    bool finished = programFinished_;
    programFinished_ = false;
    return finished;
}

void PillarPlayer::endPreview(uint32_t nowMs) {
    if (active_ == Slot::Preview) play(base(), nowMs);
}

void PillarPlayer::reload(uint32_t nowMs) {
    if (active_ != Slot::Preview) play(base(), nowMs);
}

void PillarPlayer::render(uint32_t nowMs, Rgb* frame, uint16_t count) {
    if (active_ == Slot::Preview && nowMs - activeStartMs_ >= kPreviewMaxMs) play(base(), nowMs);
    if (renderer_.render(nowMs, frame, count)) return;
    if (active_ == Slot::Program) {  // a one-shot program finished
        programActive_ = false;
        programFinished_ = true;
    }
    play(base(), nowMs);
    renderer_.render(nowMs, frame, count);
}
