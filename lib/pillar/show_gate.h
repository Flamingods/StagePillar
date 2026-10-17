#pragma once
#include <stdint.h>
#include <string.h>

// Reads GET /api/player/state: the show runs while a song is loaded (playing or
// paused), i.e. "current_song" is not null. Anything unexpected counts as not running.
inline bool parseShowRunning(const char* body) {
    if (!body) return false;
    const char* key = strstr(body, "\"current_song\"");
    if (!key) return false;
    const char* p = key + strlen("\"current_song\"");
    while (*p == ' ') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ') p++;
    return *p != '\0' && strncmp(p, "null", 4) != 0;
}

// Reads a non-negative integer field such as "pillar_plans_version" from the same body.
// Leaves out untouched and returns false if the field is missing or not a number.
inline bool parseUintField(const char* body, const char* field, uint32_t& out) {
    if (!body) return false;
    char key[48];
    size_t n = strlen(field);
    if (n + 3 > sizeof(key)) return false;
    key[0] = '"';
    memcpy(key + 1, field, n);
    key[n + 1] = '"';
    key[n + 2] = '\0';
    const char* p = strstr(body, key);
    if (!p) return false;
    p += n + 2;
    while (*p == ' ') p++;
    if (*p != ':') return false;
    p++;
    while (*p == ' ') p++;
    if (*p < '0' || *p > '9') return false;
    uint32_t value = 0;
    while (*p >= '0' && *p <= '9') value = value * 10 + uint32_t(*p++ - '0');
    out = value;
    return true;
}
