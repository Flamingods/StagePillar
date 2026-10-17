#pragma once
#include <stdint.h>
#include "rgb.h"
#include "show_events.h"

// Draws a show event's pole on the strip exactly like Tom's stage emulator
// (StageController frontend/src/show/events.ts poleState + stageLook.ts drift).

struct PaletteColor {
    char name[8];
    Rgb ramp[3];       // dim, base, bright
    float gain;
    uint32_t driftMs;  // drift period
};

struct Palette {
    uint32_t version = 0;
    uint8_t count = 0;
    PaletteColor colors[8];
};

// The spec.json values at the time of writing, used until the server's palette arrives.
Palette defaultPalette();
PaletteColor* paletteFind(Palette& palette, const char* name);
// GET /api/show/palette: {"version", "colors": {name: {"ramp", "gain", "drift_ms"}}}.
bool parsePalette(const char* json, Palette& out);

// elapsedMs: since the event arrived; nowMs: the clock that drives the colour drift.
void renderPole(const PoleState& pole, uint32_t elapsedMs, uint32_t nowMs, const Palette& palette, Rgb* frame,
                uint16_t count);
