#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Requests the pillar makes to StageController (show-events contract). Every request
// carries the pillar's side, 'L' or 'R'. Return false if out is too small.

inline bool pollPath(char* out, size_t capacity, char side, uint32_t runningVersion) {
    int n = snprintf(out, capacity, "/api/player/state?client=pillar&side=%c&running_version=%lu", side,
                     (unsigned long)runningVersion);
    return n > 0 && size_t(n) < capacity;
}

// JSON body for POST /api/buttons/press.
inline bool pressBody(char* out, size_t capacity, const char* action, char side, uint8_t taps,
                      uint32_t firstPressAgoMs) {
    int n = snprintf(out, capacity, "{\"action\":\"%s\",\"side\":\"%c\",\"taps\":%u,\"first_press_ago_ms\":%lu}",
                     action, side, unsigned(taps), (unsigned long)firstPressAgoMs);
    return n > 0 && size_t(n) < capacity;
}
