#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// Paths (with query) of the requests the pillar makes to StageController. Every
// request carries the pillar's id so the server can tell the two pillars apart.
// Return false if out is too small.

inline bool pollPath(char* out, size_t capacity, uint8_t pillarId, uint32_t runningVersion) {
    int n = snprintf(out, capacity, "/api/player/state?client=pillar&pillar_id=%u&running_version=%lu",
                     unsigned(pillarId), (unsigned long)runningVersion);
    return n > 0 && size_t(n) < capacity;
}

inline bool pressPath(char* out, size_t capacity, const char* action, uint8_t pillarId) {
    int n = snprintf(out, capacity, "/api/buttons/press/%s?pillar_id=%u", action, unsigned(pillarId));
    return n > 0 && size_t(n) < capacity;
}
