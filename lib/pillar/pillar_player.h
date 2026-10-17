#pragma once
#include <stdint.h>
#include "gesture.h"
#include "sequence.h"

// One plan per show state or gesture, as in the StageController pillar LED contract.
// Preview is the editor's draft, played on request and never stored as a slot.
// Program is the library program the Pi commanded; it replaces the idle/play base.
enum class Slot : uint8_t { None, Idle, Start, Claps, Special, Skip, Stop, Preview, Program };

constexpr uint8_t kStoredSlots = 6;  // Idle .. Stop
extern const Slot kSlots[kStoredSlots];

Slot slotFor(Gesture gesture);
// Contract name used in URLs and file names ("idle", "start", ...).
const char* slotName(Slot slot);
// What the pillar plays when no plan has been uploaded for a slot.
Plan defaultPlan(Slot slot);

// Decides what the pillar shows:
// - the idle plan while the show is not running, the play (start) loop while it is;
// - a gesture plays its plan, then returns to whichever of those applies;
// - the show ending (or a stop gesture) plays stop once, then idle;
// - a preview interrupts anything and ends on endPreview, when a one-shot preview
//   finishes, or when a gesture arrives.
class PillarPlayer {
public:
    typedef Plan (*Lookup)(Slot);

    explicit PillarPlayer(Lookup lookup = defaultPlan);

    void trigger(Slot slot, uint32_t nowMs);
    void setShowRunning(bool running, uint32_t nowMs);
    void playPreview(Plan plan, uint32_t nowMs);
    void endPreview(uint32_t nowMs);
    bool previewActive() const { return active_ == Slot::Preview; }
    // A commanded program becomes the base layer until cleared or, if it does not loop,
    // until it finishes (reported once by takeProgramFinished).
    void setProgram(Plan plan, uint32_t nowMs);
    void clearProgram(uint32_t nowMs);
    // Same program, new storage location (library swapped in, file unchanged): keep playing.
    void updateProgramPlan(Plan plan);
    bool takeProgramFinished();
    // Plans changed on disk: restart the base loop from its new plan. A one-shot ends,
    // since its file may have been replaced under it.
    void reload(uint32_t nowMs);
    void render(uint32_t nowMs, Rgb* frame, uint16_t count);

private:
    void play(Slot slot, uint32_t nowMs);
    Slot base() const { return programActive_ ? Slot::Program : running_ ? Slot::Start : Slot::Idle; }

    Lookup lookup_;
    bool running_ = false;
    Slot active_ = Slot::Idle;
    uint32_t activeStartMs_ = 0;
    Plan preview_{nullptr, nullptr};
    Plan program_{nullptr, nullptr};
    bool programActive_ = false;
    bool programFinished_ = false;
    SequenceRenderer renderer_;
};
