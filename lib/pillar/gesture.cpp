#include "gesture.h"

// Counts above 4 all map to Many; the real count (sent to the server) stops here.
static const uint8_t kMaxCount = 20;

static Gesture gestureForCount(uint8_t count) {
    switch (count) {
        case 1: return Gesture::Single;
        case 2: return Gesture::Double;
        case 3: return Gesture::Triple;
        case 4: return Gesture::Quad;
        default: return count >= 5 ? Gesture::Many : Gesture::None;
    }
}

GestureDetector::GestureDetector(GestureTiming timing) : timing_(timing) {}

Gesture GestureDetector::update(bool pressed, uint32_t nowMs) {
    if (pressed != raw_) {
        raw_ = pressed;
        rawSince_ = nowMs;
    }

    if (raw_ != stable_ && nowMs - rawSince_ >= timing_.debounceMs) {
        stable_ = raw_;
        stableSince_ = rawSince_;
        if (stable_ && count_ == 0) firstPressMs_ = stableSince_;
        if (!stable_) {
            if (longFired_) {
                longFired_ = false;
            } else if (count_ < kMaxCount) {
                count_++;
            }
        }
    }

    // raw_ must also be down: a release still in its debounce window ends the hold short of Long.
    if (stable_ && raw_ && !longFired_ && nowMs - stableSince_ >= timing_.longMs) {
        longFired_ = true;
        count_ = 0;
        lastTaps_ = 1;
        lastFirstPressMs_ = stableSince_;
        return Gesture::Long;
    }

    // raw_ must also be up: a press that has started but not yet debounced keeps the gesture open.
    if (!stable_ && !raw_ && count_ > 0 && nowMs - stableSince_ >= timing_.gapMs) {
        uint8_t count = count_;
        count_ = 0;
        lastTaps_ = count;
        lastFirstPressMs_ = firstPressMs_;
        return gestureForCount(count);
    }

    return Gesture::None;
}
