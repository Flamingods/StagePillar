#pragma once
#include <stdint.h>
#include "show_events.h"

// Which commanded program the pillar should be playing (pillar LED contract §10).
// Commands come over the WebSocket and as a backup in the state reply; only a seq newer
// than the last applied one counts. The latest command is kept even when its program is
// not in the library yet, and losing the Pi only pauses it.
class ProgramControl {
public:
    static const uint32_t kPauseAfterMs = 10000;

    // True if the command was newer and is now the current one.
    bool offer(const PillarCommand& c) {
        if (c.seq <= seq_) return false;
        seq_ = c.seq;
        program_ = c.program;
        finished_ = false;
        return true;
    }
    void pollOk(uint32_t nowMs) {
        lastPollMs_ = nowMs;
        polled_ = true;
    }
    // A one-shot program played to its end: it stays off until a new command.
    void finished() { finished_ = true; }

    // Program id to play now, or 0 for normal behaviour.
    uint16_t wanted(uint32_t nowMs) const {
        if (program_ == 0 || finished_) return 0;
        if (!polled_ || nowMs - lastPollMs_ >= kPauseAfterMs) return 0;
        return program_;
    }
    uint32_t lastSeq() const { return seq_; }

private:
    uint32_t seq_ = 0;
    uint16_t program_ = 0;
    bool finished_ = false;
    bool polled_ = false;
    uint32_t lastPollMs_ = 0;
};

constexpr uint8_t kMaxPrograms = 24;

struct ProgramIndex {
    uint32_t version = 0;
    uint8_t count = 0;
    uint8_t ids[kMaxPrograms];
};

// GET /api/pillar/programs/index: {"version":N,"ids":[...]} with ids 1..255, at most 24.
bool parseProgramIndex(const char* json, ProgramIndex& out);
